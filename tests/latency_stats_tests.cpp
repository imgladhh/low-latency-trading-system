#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include "latency_stats.h"

namespace {

bool check_eq(const char* label, const std::int64_t actual, const std::int64_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool check_eq_size(const char* label, const std::size_t actual, const std::size_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool test_empty_summary() {
    llt::LatencyStats stats;
    const llt::LatencySummary summary = stats.summarize();

    return check_eq_size("empty.count", summary.count, 0) &&
        check_eq("empty.min_ns", summary.min_ns, 0) &&
        check_eq("empty.mean_ns", summary.mean_ns, 0) &&
        check_eq("empty.p50_ns", summary.p50_ns, 0) &&
        !summary.has_p99 &&
        !summary.has_p99_9 &&
        !summary.has_tail_mean_99 &&
        check_eq("empty.max_ns", summary.max_ns, 0);
}

bool test_percentiles_and_tail_mean() {
    llt::LatencyStats stats;
    stats.initialize("test", 1000, 1);
    for (std::int64_t sample = 1; sample <= 1000; ++sample) {
        if (!stats.record(sample)) {
            return false;
        }
    }

    const llt::LatencySummary summary = stats.summarize();

    return check_eq_size("summary.count", summary.count, 1000) &&
        check_eq("summary.min_ns", summary.min_ns, 1) &&
        check_eq("summary.mean_ns", summary.mean_ns, 500) &&
        check_eq("summary.p50_ns", summary.p50_ns, 500) &&
        check_eq("summary.p99_ns", summary.p99_ns, 990) &&
        check_eq("summary.p99_9_ns", summary.p99_9_ns, 999) &&
        summary.has_p99 &&
        summary.has_p99_9 &&
        summary.has_tail_mean_99 &&
        check_eq("summary.tail_mean_99_ns", summary.tail_mean_99_ns, 995) &&
        check_eq("summary.max_ns", summary.max_ns, 1000);
}

bool test_small_sample_suppresses_high_percentiles() {
    llt::LatencyStats stats;
    stats.initialize("small", 3, 1);
    if (!stats.record(5) || !stats.record(10) || !stats.record(20)) {
        return false;
    }

    const llt::LatencySummary summary = stats.summarize();
    std::ostringstream output;
    stats.print_summary(output, "small");

    return check_eq_size("small.count", summary.count, 3) &&
        check_eq("small.p50_ns", summary.p50_ns, 10) &&
        !summary.has_p99 &&
        !summary.has_p99_9 &&
        !summary.has_tail_mean_99 &&
        output.str().find("estimator=nearest_rank") != std::string::npos &&
        output.str().find("p99_ns=n/a") != std::string::npos &&
        output.str().find("p99_9_ns=n/a") != std::string::npos &&
        output.str().find("tail_mean_99_ns=n/a") != std::string::npos;
}

bool test_drop_new_preserves_storage_and_counts_overflow() {
    llt::LatencyStats stats;
    stats.initialize("bounded", 2, 2);
    const std::int64_t* const storage = stats.storage_data();

    const bool accepted = stats.record(1) && stats.record(2) && stats.record(3) && stats.record(4);
    const bool overflow_rejected = !stats.record(999) && !stats.record(1000);
    const llt::LatencySummary summary = stats.summarize();

    return accepted && overflow_rejected &&
        stats.storage_data() == storage &&
        check_eq_size("bounded.capacity", stats.capacity(), 4) &&
        check_eq_size("bounded.count", stats.count(), 4) &&
        check_eq_size("bounded.overflow", stats.overflow_count(), 2) &&
        check_eq("bounded.max", summary.max_ns, 4);
}

bool test_capacity_multiplication_overflow_is_rejected() {
    llt::LatencyStats stats;
    try {
        stats.initialize("overflow_probe", std::numeric_limits<std::size_t>::max(), 2);
    } catch (const std::overflow_error& error) {
        const std::string message(error.what());
        return message.find("collector=overflow_probe") != std::string::npos &&
            message.find("tick_count=") != std::string::npos &&
            message.find("max_per_tick=2") != std::string::npos;
    }
    return false;
}

}  // namespace

int main() {
    const bool ok =
        test_empty_summary() &&
        test_percentiles_and_tail_mean() &&
        test_small_sample_suppresses_high_percentiles() &&
        test_drop_new_preserves_storage_and_counts_overflow() &&
        test_capacity_multiplication_overflow_is_rejected();

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "latency_stats_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
