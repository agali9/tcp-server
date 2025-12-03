#include "server.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace tds {

namespace {

constexpr int kMaxEpollEvents = 256;
constexpr int kListenBacklog = 1024;

void wake_io_thread(int event_fd) {
    const std::uint64_t wake = 1;
    if (write(event_fd, &wake, sizeof(wake)) < 0) {
        // Best-effort wake; epoll will eventually drain outbound work.
    }
}

void drain_io_thread(int event_fd) {
    std::uint64_t counter = 0;
    if (read(event_fd, &counter, sizeof(counter)) < 0) {
        // Non-blocking eventfd read can fail if already drained.
    }
}

}  // namespace

TcpDataServer::TcpDataServer(ServerConfig config) : config_(std::move(config)) {
    if (config_.worker_threads == 0) {
        config_.worker_threads = 1;
    }
}

TcpDataServer::~TcpDataServer() {
    stop();
}

void TcpDataServer::set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw std::runtime_error("fcntl F_GETFL failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("fcntl F_SETFL O_NONBLOCK failed");
    }
}

void TcpDataServer::setup_listening_socket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error("socket() failed");
    }

    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    set_nonblocking(listen_fd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("invalid bind address");
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind() failed");
    }
    if (listen(listen_fd_, kListenBacklog) < 0) {
        throw std::runtime_error("listen() failed");
    }

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("epoll_create1() failed");
    }

    event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd_ < 0) {
        throw std::runtime_error("eventfd() failed");
    }

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.u64 = 0;  // reserved for listen socket
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        throw std::runtime_error("epoll_ctl listen failed");
    }

    ev.data.u64 = UINT64_MAX;  // reserved for eventfd
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev) < 0) {
        throw std::runtime_error("epoll_ctl eventfd failed");
    }
}

void TcpDataServer::register_connection(int fd) {
    if (connections_.size() >= config_.max_connections) {
        close(fd);
        metrics_.queue_drops.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    set_nonblocking(fd);

    auto conn = std::make_unique<Connection>(fd);
    const std::uint64_t conn_id = conn->id();

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.u64 = conn_id;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        conn->close_fd();
        throw std::runtime_error("epoll_ctl add client failed");
    }

    connections_.emplace(conn_id, std::move(conn));
    metrics_.connections_accepted.fetch_add(1, std::memory_order_relaxed);
    metrics_.connections_active.fetch_add(1, std::memory_order_relaxed);
}

void TcpDataServer::unregister_connection(std::uint64_t conn_id) {
    if (conn_id == 0 || conn_id == UINT64_MAX) {
        return;
    }
    const auto it = connections_.find(conn_id);
    if (it == connections_.end()) {
        return;
    }
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, it->second->fd(), nullptr);
    it->second->close_fd();
    connections_.erase(it);
    metrics_.connections_active.fetch_sub(1, std::memory_order_relaxed);
}

void TcpDataServer::update_epoll_interest(std::uint64_t conn_id, bool want_write) {
    const auto it = connections_.find(conn_id);
    if (it == connections_.end()) {
        return;
    }
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (want_write) {
        ev.events |= EPOLLOUT;
    }
    ev.data.u64 = conn_id;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, it->second->fd(), &ev);
}

void TcpDataServer::handle_accept() {
    for (;;) {
        const int client_fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            throw std::runtime_error("accept4() failed");
        }
        register_connection(client_fd);
    }
}

void TcpDataServer::handle_read(std::uint64_t conn_id) {
    const auto it = connections_.find(conn_id);
    if (it == connections_.end()) {
        return;
    }

    Connection& conn = *it->second;
    std::vector<std::uint8_t> buf(config_.read_buffer_size);

    for (;;) {
        const ssize_t n = recv(conn.fd(), buf.data(), buf.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            unregister_connection(conn_id);
            return;
        }
        if (n == 0) {
            unregister_connection(conn_id);
            return;
        }

        metrics_.bytes_received.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
        conn.append_read(buf.data(), static_cast<std::size_t>(n));

        try {
            std::vector<Message> messages = conn.drain_complete_messages();
            for (Message& msg : messages) {
                WorkItem item;
                item.conn_id = conn_id;
                item.request = std::move(msg);
                item.received_at = std::chrono::steady_clock::now();

                if (!inbound_.try_push(std::move(item))) {
                    metrics_.queue_drops.fetch_add(1, std::memory_order_relaxed);
                    OutboundItem reject;
                    reject.conn_id = conn_id;
                    reject.frame = build_error_frame(ErrorCode::Internal, "server overloaded");
                    if (outbound_.try_push(std::move(reject))) {
                        wake_io_thread(event_fd_);
                    }
                }
            }
        } catch (const std::exception&) {
            metrics_.parse_errors.fetch_add(1, std::memory_order_relaxed);
            unregister_connection(conn_id);
            return;
        }
    }
}

void TcpDataServer::drain_outbound_queue() {
    for (;;) {
        auto item = outbound_.try_pop();
        if (!item) {
            break;
        }

        const auto it = connections_.find(item->conn_id);
        if (it == connections_.end()) {
            continue;
        }

        Connection& conn = *it->second;
        metrics_.bytes_sent.fetch_add(item->frame.size(), std::memory_order_relaxed);
        conn.enqueue_write(std::move(item->frame));
        update_epoll_interest(item->conn_id, true);
    }
}

std::vector<std::uint8_t> TcpDataServer::build_error_frame(ErrorCode code, std::string_view detail) {
    std::vector<std::uint8_t> payload;
    payload.push_back(static_cast<std::uint8_t>(code));
    payload.insert(payload.end(), detail.begin(), detail.end());
    return encode_message(MessageType::Error, payload);
}

Message TcpDataServer::process_request(const Message& request) {
    switch (request.type) {
        case MessageType::Ping: {
            Message response;
            response.type = MessageType::Pong;
            return response;
        }
        case MessageType::Echo: {
            Message response;
            response.type = MessageType::Echo;
            response.payload = request.payload;
            return response;
        }
        case MessageType::Get: {
            const auto key = decode_kv_key(
                std::string_view(reinterpret_cast<const char*>(request.payload.data()), request.payload.size()));
            Message response;
            response.type = MessageType::GetResponse;
            if (!key) {
                response.payload = {static_cast<std::uint8_t>(ErrorCode::BadRequest)};
                return response;
            }
            const auto value = store_.get(*key);
            if (!value) {
                response.payload = {static_cast<std::uint8_t>(ErrorCode::NotFound)};
                return response;
            }
            response.payload.push_back(static_cast<std::uint8_t>(ErrorCode::None));
            response.payload.insert(response.payload.end(), value->begin(), value->end());
            return response;
        }
        case MessageType::Put: {
            const auto kv = decode_kv_put(
                std::string_view(reinterpret_cast<const char*>(request.payload.data()), request.payload.size()));
            Message response;
            response.type = MessageType::PutResponse;
            if (!kv) {
                response.payload = {static_cast<std::uint8_t>(ErrorCode::BadRequest)};
                return response;
            }
            store_.put(kv->first, kv->second);
            response.payload = {static_cast<std::uint8_t>(ErrorCode::None)};
            return response;
        }
        case MessageType::Stats: {
            Message response;
            response.type = MessageType::StatsResponse;
            const std::string json = metrics_.snapshot_json();
            response.payload.assign(json.begin(), json.end());
            return response;
        }
        default: {
            Message response;
            response.type = MessageType::Error;
            response.payload = {static_cast<std::uint8_t>(ErrorCode::BadRequest)};
            return response;
        }
    }
}

void TcpDataServer::worker_loop() {
    while (running_.load(std::memory_order_acquire)) {
        auto item = inbound_.try_pop();
        if (!item) {
            std::this_thread::yield();
            continue;
        }

        const auto start = item->received_at;
        Message response = process_request(item->request);

        const auto end = std::chrono::steady_clock::now();
        metrics_.request_latency.record(end - start);
        metrics_.requests_processed.fetch_add(1, std::memory_order_relaxed);

        OutboundItem outbound;
        outbound.conn_id = item->conn_id;
        outbound.frame = encode_message(response.type, response.payload);

        while (running_.load(std::memory_order_acquire)) {
            if (outbound_.try_push(std::move(outbound))) {
                wake_io_thread(event_fd_);
                break;
            }
            std::this_thread::yield();
        }
    }
}

void TcpDataServer::io_loop() {
    std::vector<epoll_event> events(kMaxEpollEvents);

    while (running_.load(std::memory_order_acquire)) {
        drain_outbound_queue();

        const int n = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), 500);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("epoll_wait() failed");
        }

        for (int i = 0; i < n; ++i) {
            const std::uint64_t id = events[i].data.u64;

            if (id == 0) {
                handle_accept();
                continue;
            }
            if (id == UINT64_MAX) {
                drain_io_thread(event_fd_);
                drain_outbound_queue();
                continue;
            }

            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                unregister_connection(id);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                handle_read(id);
            }

            if (events[i].events & EPOLLOUT) {
                const auto it = connections_.find(id);
                if (it != connections_.end()) {
                    if (!it->second->flush_writes()) {
                        unregister_connection(id);
                        continue;
                    }
                    if (!it->second->has_pending_writes()) {
                        update_epoll_interest(id, false);
                    }
                }
            }
        }
    }
}

void TcpDataServer::run() {
    setup_listening_socket();
    running_.store(true, std::memory_order_release);

    workers_.reserve(config_.worker_threads);
    for (std::size_t i = 0; i < config_.worker_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }

    std::cout << "tds_server listening on " << config_.bind_address << ":" << config_.port
              << " workers=" << config_.worker_threads << std::endl;

    io_loop();
}

void TcpDataServer::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (event_fd_ >= 0) {
        wake_io_thread(event_fd_);
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    for (auto& [id, conn] : connections_) {
        (void)id;
        conn->close_fd();
    }
    connections_.clear();

    if (event_fd_ >= 0) {
        close(event_fd_);
        event_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

}  // namespace tds
