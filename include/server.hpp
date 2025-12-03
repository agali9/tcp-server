#pragma once

#include "connection.hpp"
#include "kv_store.hpp"
#include "metrics.hpp"
#include "mpmc_queue.hpp"
#include "protocol.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tds {

struct ServerConfig {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{9000};
    std::size_t worker_threads{std::thread::hardware_concurrency()};
    std::size_t max_connections{4096};
    std::size_t inbound_queue_capacity{8192};
    std::size_t outbound_queue_capacity{8192};
    std::size_t read_buffer_size{8192};
};

class TcpDataServer {
public:
    explicit TcpDataServer(ServerConfig config);
    ~TcpDataServer();

    TcpDataServer(const TcpDataServer&) = delete;
    TcpDataServer& operator=(const TcpDataServer&) = delete;

    void run();
    void stop();

    [[nodiscard]] const ServerMetrics& metrics() const noexcept { return metrics_; }

private:
    static constexpr std::size_t kInboundCapacity = 8192;
    static constexpr std::size_t kOutboundCapacity = 8192;

    void setup_listening_socket();
    void io_loop();
    void worker_loop();
    void handle_accept();
    void handle_read(std::uint64_t conn_id);
    void drain_outbound_queue();
    void register_connection(int fd);
    void unregister_connection(std::uint64_t conn_id);
    void set_nonblocking(int fd);
    void update_epoll_interest(std::uint64_t conn_id, bool want_write);

    Message process_request(const Message& request);
    std::vector<std::uint8_t> build_error_frame(ErrorCode code, std::string_view detail);

    ServerConfig config_;
    ServerMetrics metrics_;
    KvStore store_;

    int listen_fd_{-1};
    int epoll_fd_{-1};
    int event_fd_{-1};  // wake I/O thread on outbound work

    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;

    MpmcQueue<WorkItem, kInboundCapacity> inbound_;
    MpmcQueue<OutboundItem, kOutboundCapacity> outbound_;

    std::unordered_map<std::uint64_t, std::unique_ptr<Connection>> connections_;
};

}  // namespace tds
