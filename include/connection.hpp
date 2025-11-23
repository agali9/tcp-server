#pragma once

#include "protocol.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

namespace tds {

struct WorkItem {
    std::uint64_t conn_id{0};
    Message request;
    std::chrono::steady_clock::time_point received_at;
};

struct OutboundItem {
    std::uint64_t conn_id{0};
    std::vector<std::uint8_t> frame;
};

// Per-connection read buffer and outbound write queue.
// Write queue is only accessed from the I/O thread; read buffer is
// filled on I/O thread before dispatching complete frames to workers.
class Connection {
public:
    explicit Connection(int fd);

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

    void append_read(const std::uint8_t* data, std::size_t len);
    [[nodiscard]] std::vector<Message> drain_complete_messages();

    void enqueue_write(std::vector<std::uint8_t> frame);
    [[nodiscard]] bool has_pending_writes() const noexcept;
    bool flush_writes();

    void close_fd();
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    static std::atomic<std::uint64_t> next_id_;

    int fd_;
    std::uint64_t id_;
    std::vector<std::uint8_t> read_buffer_;
    std::deque<std::vector<std::uint8_t>> write_queue_;
    std::vector<std::uint8_t> write_cursor_;
};

}  // namespace tds
