#include "kv_store.hpp"

#include <functional>

namespace tds {

KvStore::KvStore(std::size_t shard_count) : shards_(shard_count) {}

void KvStore::put(std::string_view key, std::string value) {
    auto& shard = shards_[shard_index(key)];
    std::unique_lock lock(shard.mutex);
    shard.map[std::string(key)] = std::move(value);
}

std::optional<std::string> KvStore::get(std::string_view key) const {
    const auto& shard = shards_[shard_index(key)];
    std::shared_lock lock(shard.mutex);
    const auto it = shard.map.find(std::string(key));
    if (it == shard.map.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t KvStore::size() const {
    std::size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        total += shard.map.size();
    }
    return total;
}

std::size_t KvStore::shard_index(std::string_view key) const {
    return std::hash<std::string_view>{}(key) % shards_.size();
}

}  // namespace tds
