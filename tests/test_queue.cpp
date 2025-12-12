#include "mpmc_queue.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    constexpr std::size_t kCapacity = 1024;
    tds::MpmcQueue<int, kCapacity> queue;

    constexpr int kTotalItems = 100000;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    const auto producer = [&] {
        for (int i = 0; i < kTotalItems / 2;) {
            if (queue.try_push(i)) {
                ++produced;
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    };

    const auto consumer = [&] {
        while (consumed.load() < kTotalItems) {
            auto item = queue.try_pop();
            if (item) {
                consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    };

    std::thread p1(producer);
    std::thread p2(producer);
    std::thread c1(consumer);
    std::thread c2(consumer);

    p1.join();
    p2.join();
    c1.join();
    c2.join();

    if (produced.load() != kTotalItems) {
        std::cerr << "producer count mismatch\n";
        return 1;
    }
    if (consumed.load() != kTotalItems) {
        std::cerr << "consumer count mismatch\n";
        return 1;
    }

    std::cout << "test_queue ok\n";
    return 0;
}
