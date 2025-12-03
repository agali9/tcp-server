#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tds {

class LatencyTracker {
public:
    explicit LatencyTracker(std::size_t reservoir_size = 100000);

    void record(std::chrono::nanoseconds latency);
    [[nodiscard]] double percentile(double p) const;
    [[nodiscard]] std::size_t sample_count() const;
    void reset();

private:
    mutable std::mutex mutex_;
    std::vector<std::uint64_t> samples_;
    std::size_t reservoir_size_;
    std::size_t total_recorded_{0};
};

struct ServerMetrics {
    std::atomic<std::uint64_t> connections_accepted{0};
    std::atomic<std::uint64_t> connections_active{0};
    std::atomic<std::uint64_t> requests_processed{0};
    std::atomic<std::uint64_t> bytes_received{0};
    std::atomic<std::uint64_t> bytes_sent{0};
    std::atomic<std::uint64_t> queue_drops{0};
    std::atomic<std::uint64_t> parse_errors{0};

    LatencyTracker request_latency;

    [[nodiscard]] std::string snapshot_json() const;
};

}  // namespace tds
