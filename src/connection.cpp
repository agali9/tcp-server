#include "connection.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace tds {

std::atomic<std::uint64_t> Connection::next_id_{1};

Connection::Connection(int fd) : fd_(fd), id_(next_id_.fetch_add(1, std::memory_order_relaxed)) {}

void Connection::append_read(const std::uint8_t* data, std::size_t len) {
    read_buffer_.insert(read_buffer_.end(), data, data + len);
}

std::vector<Message> Connection::drain_complete_messages() {
    std::vector<Message> messages;
    for (;;) {
        try {
            const FrameParseResult parsed = try_parse_frame(read_buffer_);
            if (!parsed.complete) {
                break;
            }
            messages.push_back(std::move(parsed.message));
            read_buffer_.erase(read_buffer_.begin(),
                               read_buffer_.begin() + static_cast<std::ptrdiff_t>(parsed.bytes_consumed));
        } catch (const std::exception&) {
            read_buffer_.clear();
            throw;
        }
    }
    return messages;
}

void Connection::enqueue_write(std::vector<std::uint8_t> frame) {
    write_queue_.push_back(std::move(frame));
}

bool Connection::has_pending_writes() const noexcept {
    return !write_queue_.empty() || !write_cursor_.empty();
}

bool Connection::flush_writes() {
    if (write_cursor_.empty() && !write_queue_.empty()) {
        write_cursor_ = std::move(write_queue_.front());
        write_queue_.pop_front();
    }

    while (!write_cursor_.empty()) {
        const ssize_t sent = ::send(fd_, write_cursor_.data(), write_cursor_.size(), MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        write_cursor_.erase(write_cursor_.begin(),
                            write_cursor_.begin() + static_cast<std::ptrdiff_t>(sent));
    }
    return true;
}

void Connection::close_fd() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace tds
