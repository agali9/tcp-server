#pragma once

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tds {

// Sharded in-memory KV store: 64 shards, each with its own shared_mutex.
// Reduces contention vs a single global lock under concurrent GET/PUT.
class KvStore {
public:
    explicit KvStore(std::size_t shard_count = 64);

    void put(std::string_view key, std::string value);
    std::optional<std::string> get(std::string_view key) const;
    [[nodiscard]] std::size_t size() const;

private:
    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, std::string> map;
    };

    [[nodiscard]] std::size_t shard_index(std::string_view key) const;

    std::vector<Shard> shards_;
};

}  // namespace tds
