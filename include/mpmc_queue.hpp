#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace tds {

// Bounded MPMC lock-free queue (Vyukov sequence-lock ring buffer).
// Capacity must be a power of two. Multiple producers and consumers
// synchronize via per-slot sequence numbers and CAS on head/tail.
template <typename T, std::size_t Capacity>
class MpmcQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
    static_assert(std::is_move_constructible_v<T>, "T must be move constructible");

public:
    MpmcQueue() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    bool try_push(T item) {
        std::size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            auto& cell = buffer_[pos & kMask];
            const std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            const std::intptr_t diff = static_cast<std::intptr_t>(seq) -
                                       static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = std::move(item);
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;  // queue full
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }

    std::optional<T> try_pop() {
        std::size_t pos = tail_.load(std::memory_order_relaxed);
        for (;;) {
            auto& cell = buffer_[pos & kMask];
            const std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            const std::intptr_t diff = static_cast<std::intptr_t>(seq) -
                                       static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    T item = std::move(cell.data);
                    cell.sequence.store(pos + Capacity, std::memory_order_release);
                    return item;
                }
            } else if (diff < 0) {
                return std::nullopt;  // queue empty
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return Capacity; }

    [[nodiscard]] std::size_t approximate_size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        return h >= t ? h - t : 0;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    struct Cell {
        std::atomic<std::size_t> sequence{0};
        T data;
    };

    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    Cell buffer_[Capacity];
};

}  // namespace tds
