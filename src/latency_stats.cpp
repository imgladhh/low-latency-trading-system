#include "latency_stats.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>

namespace llt {

void LatencyStats::initialize(
    const std::string_view name,
    const std::size_t tick_count,
    const std::size_t max_per_tick) {
    const auto fail = [&]() {
        throw std::overflow_error(
            "latency collector capacity invalid: collector=" + std::string(name) +
            " tick_count=" + std::to_string(tick_count) +
            " max_per_tick=" + std::to_string(max_per_tick));
    };

    if (max_per_tick == 0 || tick_count > std::numeric_limits<std::size_t>::max() / max_per_tick) {
        fail();
    }

    const std::size_t capacity = tick_count * max_per_tick;
    if (capacity > samples_.max_size()) {
        fail();
    }

    samples_.assign(capacity, 0);
    sample_count_ = 0;
    overflow_count_ = 0;
    max_per_tick_ = max_per_tick;
}

bool LatencyStats::record(const std::int64_t latency_ns) noexcept {
    if (sample_count_ == samples_.size()) {
        ++overflow_count_;
        return false;
    }

    samples_[sample_count_++] = latency_ns;
    return true;
}

LatencySummary LatencyStats::summarize() const {
    if (sample_count_ == 0) {
        return LatencySummary{0, 0, 0, 0, 0, 0, false, false, 0, false, 0};
    }

    std::vector<std::int64_t> sorted(samples_.begin(), samples_.begin() + sample_count_);
    // Summaries are computed off the hot path, so we keep the per-sample recording logic minimal
    // and do the copy/sort work only when we explicitly print or inspect the distribution.
    std::sort(sorted.begin(), sorted.end());

    std::int64_t total_ns = 0;
    for (const std::int64_t sample : sorted) {
        total_ns += sample;
    }

    const bool has_p99 = sorted.size() >= p99_min_samples;
    const bool has_p99_9 = sorted.size() >= p99_9_min_samples;
    const std::size_t tail_count = has_p99 ? tail_count_for_top_percent(sorted.size(), 1) : 0;
    const std::size_t tail_start = sorted.size() - tail_count;
    std::int64_t tail_total_ns = 0;
    for (std::size_t index = tail_start; index < sorted.size(); ++index) {
        tail_total_ns += sorted[index];
    }

    return LatencySummary{
        sorted.size(),
        sorted.front(),
        total_ns / static_cast<std::int64_t>(sorted.size()),
        sorted[percentile_index_permille(sorted.size(), 500)],
        has_p99 ? sorted[percentile_index_permille(sorted.size(), 990)] : 0,
        has_p99_9 ? sorted[percentile_index_permille(sorted.size(), 999)] : 0,
        has_p99,
        has_p99_9,
        has_p99 ? tail_total_ns / static_cast<std::int64_t>(tail_count) : 0,
        has_p99,
        sorted.back(),
    };
}

void LatencyStats::print_summary(std::ostream& out, const std::string_view name) const {
    const LatencySummary summary = summarize();
    out
        << "latency[" << name << "] "
        << "count=" << summary.count
        << " overflow=" << overflow_count_
        << " capacity=" << samples_.size()
        << " max_per_tick=" << max_per_tick_
        << " estimator=nearest_rank"
        << " min_ns=" << summary.min_ns
        << " mean_ns=" << summary.mean_ns
        << " p50_ns=" << summary.p50_ns;
    out << " p99_ns=";
    if (summary.has_p99) {
        out << summary.p99_ns;
    } else {
        out << "n/a";
    }
    out << " p99_9_ns=";
    if (summary.has_p99_9) {
        out << summary.p99_9_ns;
    } else {
        out << "n/a";
    }
    out << " tail_mean_99_ns=";
    if (summary.has_tail_mean_99) {
        out << summary.tail_mean_99_ns;
    } else {
        out << "n/a";
    }
    out << " max_ns=" << summary.max_ns << '\n';
}

std::size_t LatencyStats::percentile_index_permille(const std::size_t n, const std::size_t permille) noexcept {
    if (n == 0) {
        return 0;
    }

    // Nearest-rank uses ceil(n * p) - 1. Splitting n into quotient/remainder avoids
    // overflowing size_t when a very large, but otherwise valid, sample set is summarized.
    const std::size_t rank = (n / 1000) * permille +
        (((n % 1000) * permille + 999) / 1000);
    return rank == 0 ? 0 : rank - 1;
}

std::size_t LatencyStats::tail_count_for_top_percent(const std::size_t n, const std::size_t percent) noexcept {
    if (n == 0) {
        return 0;
    }

    const std::size_t tail_count = (n / 100) * percent +
        (((n % 100) * percent + 99) / 100);
    return tail_count == 0 ? 1 : tail_count;
}

}  // namespace llt
