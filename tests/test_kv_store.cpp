#include "kv_store.hpp"

#include <iostream>
#include <thread>
#include <vector>

int main() {
    tds::KvStore store(16);

    constexpr int kWriters = 8;
    constexpr int kReaders = 8;
    constexpr int kOps = 1000;

    std::vector<std::thread> threads;

    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w] {
            for (int i = 0; i < kOps; ++i) {
                store.put("key" + std::to_string(i % 100), "value" + std::to_string(w));
            }
        });
    }

    for (int r = 0; r < kReaders; ++r) {
        threads.emplace_back([&] {
            for (int i = 0; i < kOps; ++i) {
                (void)store.get("key" + std::to_string(i % 100));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    if (store.size() == 0) {
        std::cerr << "expected non-empty store\n";
        return 1;
    }

    std::cout << "test_kv_store ok size=" << store.size() << "\n";
    return 0;
}
