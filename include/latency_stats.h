#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string_view>
#include <vector>

namespace llt {

struct LatencySummary {
    std::size_t count;
    std::int64_t min_ns;
    std::int64_t mean_ns;
    std::int64_t p50_ns;
    std::int64_t p99_ns;
    std::int64_t p99_9_ns;
    bool has_p99;
    bool has_p99_9;
    // tail_mean_99 is the average of the slowest 1% of samples, which is useful when we want
    // a latency analogue of CVaR/expected shortfall rather than just a single percentile cutoff.
    std::int64_t tail_mean_99_ns;
    bool has_tail_mean_99;
    std::int64_t max_ns;
};

struct LatencySamplesView {
    const std::int64_t* data;
    std::size_t count;
    [[nodiscard]] const std::int64_t* begin() const noexcept { return data; }
    [[nodiscard]] const std::int64_t* end() const noexcept { return data + count; }
};

class LatencyStats {
public:
    static constexpr std::size_t p99_min_samples = 100;
    static constexpr std::size_t p99_9_min_samples = 1000;

    void initialize(std::string_view name, std::size_t tick_count, std::size_t max_per_tick);
    [[nodiscard]] bool record(std::int64_t latency_ns) noexcept;
    [[nodiscard]] bool empty() const noexcept { return sample_count_ == 0; }
    [[nodiscard]] std::size_t count() const noexcept { return sample_count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return samples_.size(); }
    [[nodiscard]] std::size_t max_per_tick() const noexcept { return max_per_tick_; }
    [[nodiscard]] std::size_t overflow_count() const noexcept { return overflow_count_; }
    [[nodiscard]] const std::int64_t* storage_data() const noexcept { return samples_.data(); }
    [[nodiscard]] LatencySamplesView samples() const noexcept {
        return {samples_.data(), sample_count_};
    }
    [[nodiscard]] LatencySummary summarize() const;
    void print_summary(std::ostream& out, std::string_view name) const;

private:
    [[nodiscard]] static std::size_t percentile_index_permille(std::size_t n, std::size_t permille) noexcept;
    [[nodiscard]] static std::size_t tail_count_for_top_percent(std::size_t n, std::size_t percent) noexcept;

    std::vector<std::int64_t> samples_;
    std::size_t sample_count_{0};
    std::size_t overflow_count_{0};
    std::size_t max_per_tick_{0};
};

}  // namespace llt
