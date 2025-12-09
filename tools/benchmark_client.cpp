#include "protocol.hpp"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct ClientConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{9000};
    std::size_t connections{200};
    std::size_t requests_per_connection{1000};
    std::size_t payload_bytes{64};
    double duration_seconds{0.0};
    std::string mode{"ping"};
};

struct Stats {
    std::atomic<std::uint64_t> completed{0};
    std::atomic<std::uint64_t> errors{0};
    std::vector<std::uint64_t> latencies_us;
    std::mutex latencies_mutex;
};

int connect_to(const std::string& host, std::uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const std::vector<std::uint8_t>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_frame(int fd, std::vector<std::uint8_t>& buffer, tds::Message& out) {
    for (;;) {
        if (buffer.size() >= tds::kHeaderSize) {
            try {
                const tds::FrameParseResult parsed = tds::try_parse_frame(buffer);
                if (parsed.complete) {
                    buffer.erase(buffer.begin(),
                                 buffer.begin() + static_cast<std::ptrdiff_t>(parsed.bytes_consumed));
                    out = std::move(parsed.message);
                    return true;
                }
            } catch (...) {
                return false;
            }
        }

        std::uint8_t chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return false;
        }
        buffer.insert(buffer.end(), chunk, chunk + n);
    }
}

tds::MessageType expected_response(tds::MessageType request) {
    switch (request) {
        case tds::MessageType::Ping:
            return tds::MessageType::Pong;
        case tds::MessageType::Echo:
            return tds::MessageType::Echo;
        case tds::MessageType::Get:
            return tds::MessageType::GetResponse;
        case tds::MessageType::Put:
            return tds::MessageType::PutResponse;
        case tds::MessageType::Stats:
            return tds::MessageType::StatsResponse;
        default:
            return tds::MessageType::Error;
    }
}

void worker_thread(const ClientConfig& cfg, Stats& stats, std::size_t conn_index) {
    const int fd = connect_to(cfg.host, cfg.port);
    if (fd < 0) {
        stats.errors.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::vector<std::uint8_t> read_buffer;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double>(cfg.duration_seconds);

    std::size_t sent = 0;
    while (cfg.duration_seconds > 0.0
               ? std::chrono::steady_clock::now() < deadline
               : sent < cfg.requests_per_connection) {
        tds::MessageType req_type = tds::MessageType::Ping;
        std::vector<std::uint8_t> payload;

        if (cfg.mode == "echo") {
            req_type = tds::MessageType::Echo;
            payload.assign(cfg.payload_bytes, 'x');
        } else if (cfg.mode == "put") {
            req_type = tds::MessageType::Put;
            const std::string key = "k" + std::to_string(conn_index) + "_" + std::to_string(sent);
            const std::string value(cfg.payload_bytes, 'v');
            payload = tds::encode_put_request(key, value);
        } else if (cfg.mode == "get") {
            req_type = tds::MessageType::Get;
            const std::string key = "k" + std::to_string(conn_index % 10) + "_0";
            payload = tds::encode_get_request(key);
        }

        const auto frame = tds::encode_message(req_type, payload);
        const auto t0 = std::chrono::steady_clock::now();

        if (!send_all(fd, frame)) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        tds::Message response;
        if (!recv_frame(fd, read_buffer, response)) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        const auto t1 = std::chrono::steady_clock::now();
        if (response.type != expected_response(req_type)) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        const auto us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

        {
            std::lock_guard lock(stats.latencies_mutex);
            stats.latencies_us.push_back(us);
        }

        stats.completed.fetch_add(1, std::memory_order_relaxed);
        ++sent;
    }

    close(fd);
}

double percentile(std::vector<std::uint64_t> samples, double p) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const double rank = (p / 100.0) * static_cast<double>(samples.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(rank);
    const std::size_t hi = std::min(lo + 1, samples.size() - 1);
    const double frac = rank - static_cast<double>(lo);
    return static_cast<double>(samples[lo]) + frac * (static_cast<double>(samples[hi]) - static_cast<double>(samples[lo]));
}

ClientConfig parse_args(int argc, char* argv[]) {
    ClientConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            cfg.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--connections" && i + 1 < argc) {
            cfg.connections = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--requests" && i + 1 < argc) {
            cfg.requests_per_connection = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration" && i + 1 < argc) {
            cfg.duration_seconds = std::stod(argv[++i]);
        } else if (arg == "--payload" && i + 1 < argc) {
            cfg.payload_bytes = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--mode" && i + 1 < argc) {
            cfg.mode = argv[++i];
        }
    }
    return cfg;
}

}  // namespace

int main(int argc, char* argv[]) {
    const ClientConfig cfg = parse_args(argc, argv);

    Stats stats;
    const auto t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(cfg.connections);
    for (std::size_t i = 0; i < cfg.connections; ++i) {
        threads.emplace_back(worker_thread, std::cref(cfg), std::ref(stats), i);
    }
    for (auto& t : threads) {
        t.join();
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double rps = stats.completed.load() / elapsed;

    std::vector<std::uint64_t> latencies;
    {
        std::lock_guard lock(stats.latencies_mutex);
        latencies = stats.latencies_us;
    }

    std::cout << "benchmark results\n"
              << "  connections: " << cfg.connections << "\n"
              << "  mode: " << cfg.mode << "\n"
              << "  completed: " << stats.completed.load() << "\n"
              << "  errors: " << stats.errors.load() << "\n"
              << "  elapsed_s: " << elapsed << "\n"
              << "  throughput_rps: " << static_cast<std::uint64_t>(rps) << "\n"
              << "  latency_p50_us: " << percentile(latencies, 50.0) << "\n"
              << "  latency_p99_us: " << percentile(latencies, 99.0) << "\n"
              << "  latency_p999_us: " << percentile(latencies, 99.9) << "\n";

    return stats.errors.load() > 0 ? 1 : 0;
}
