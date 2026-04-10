#pragma once

#include <cstdint>
#include <mutex>

namespace doly::eye_engine {

struct SampleStats {
    std::uint64_t count{0};
    double mean_ns{0.0};
    double min_ns{0.0};
    double max_ns{0.0};
};

/**
 * Thread-safe Welford online variance/mean accumulator (tracking min/max).
 */
class SampleAggregator {
public:
    void addSample(double value_ns);
    SampleStats snapshot() const;

private:
    mutable std::mutex mutex_;
    std::uint64_t count_{0};
    double mean_{0.0};
    double min_{0.0};
    double max_{0.0};
};

}  // namespace doly::eye_engine
