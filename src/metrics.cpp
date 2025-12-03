#include "metrics.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace tds {

LatencyTracker::LatencyTracker(std::size_t reservoir_size)
    : reservoir_size_(reservoir_size) {
    samples_.reserve(reservoir_size_);
}

void LatencyTracker::record(std::chrono::nanoseconds latency) {
    const auto ns = static_cast<std::uint64_t>(latency.count());
    std::lock_guard lock(mutex_);
    ++total_recorded_;
    if (samples_.size() < reservoir_size_) {
        samples_.push_back(ns);
        return;
    }
    // Reservoir sampling: replace random element once full.
    const std::size_t idx = total_recorded_ % reservoir_size_;
    samples_[idx] = ns;
}

double LatencyTracker::percentile(double p) const {
    if (p < 0.0 || p > 100.0) {
        return 0.0;
    }
    std::lock_guard lock(mutex_);
    if (samples_.empty()) {
        return 0.0;
    }
    std::vector<std::uint64_t> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    const double rank = (p / 100.0) * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(rank);
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = rank - static_cast<double>(lo);
    const double lo_val = static_cast<double>(sorted[lo]);
    const double hi_val = static_cast<double>(sorted[hi]);
    return lo_val + frac * (hi_val - lo_val);
}

std::size_t LatencyTracker::sample_count() const {
    std::lock_guard lock(mutex_);
    return samples_.size();
}

void LatencyTracker::reset() {
    std::lock_guard lock(mutex_);
    samples_.clear();
    total_recorded_ = 0;
}

std::string ServerMetrics::snapshot_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"connections_accepted\":" << connections_accepted.load() << ","
        << "\"connections_active\":" << connections_active.load() << ","
        << "\"requests_processed\":" << requests_processed.load() << ","
        << "\"bytes_received\":" << bytes_received.load() << ","
        << "\"bytes_sent\":" << bytes_sent.load() << ","
        << "\"queue_drops\":" << queue_drops.load() << ","
        << "\"parse_errors\":" << parse_errors.load() << ","
        << "\"latency_p50_us\":" << (request_latency.percentile(50.0) / 1000.0) << ","
        << "\"latency_p99_us\":" << (request_latency.percentile(99.0) / 1000.0) << ","
        << "\"latency_p999_us\":" << (request_latency.percentile(99.9) / 1000.0)
        << "}";
    return oss.str();
}

}  // namespace tds
