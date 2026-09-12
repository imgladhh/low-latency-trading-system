#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "csv_reader.h"
#include "replay_runner.h"

#ifndef LLT_BUILD_TYPE
#define LLT_BUILD_TYPE "unknown"
#endif
#ifndef LLT_GIT_COMMIT
#define LLT_GIT_COMMIT "unknown"
#endif

namespace {

struct Options {
    const char* input{nullptr};
    std::size_t warmup{2};
    std::size_t measured{5};
    const char* csv_path{nullptr};
};

std::size_t parse_positive(const char* text, const char* name) {
    std::size_t value = 0;
    const std::string_view input(text == nullptr ? "" : text);
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    if (error != std::errc{} || end != input.data() + input.size() || value == 0) {
        throw std::invalid_argument(std::string(name) + " must be a positive integer");
    }
    return value;
}

Options parse_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::invalid_argument(
            "usage: bench_replay <csv-path> [--warmup N] [--measured N] [--emit-csv path]");
    }
    Options options{};
    options.input = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--warmup" || option == "--measured" || option == "--emit-csv") && i + 1 >= argc) {
            throw std::invalid_argument(std::string(option) + " requires a value");
        }
        if (option == "--warmup") options.warmup = parse_positive(argv[++i], "warmup");
        else if (option == "--measured") options.measured = parse_positive(argv[++i], "measured");
        else if (option == "--emit-csv") options.csv_path = argv[++i];
        else throw std::invalid_argument("unknown option: " + std::string(option));
    }
    return options;
}

std::uint64_t file_hash(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to hash input dataset");
    std::uint64_t hash = 1469598103934665603ULL;
    char byte = 0;
    while (input.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

class ChecksumSink final : public llt::ReplayEventSink {
public:
    bool consume(const llt::TradeEvent& event) noexcept override {
        checksum_ ^= static_cast<std::uint64_t>(event.ts_ns) + 0x9e3779b97f4a7c15ULL +
            (checksum_ << 6) + (checksum_ >> 2);
        checksum_ ^= static_cast<std::uint64_t>(event.price);
        checksum_ ^= static_cast<std::uint64_t>(event.quantity) << 17;
        ++count_;
        return true;
    }
    [[nodiscard]] std::uint64_t checksum() const noexcept { return checksum_; }
    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
private:
    std::uint64_t checksum_{0};
    std::uint64_t count_{0};
};

struct Aggregates {
    llt::LatencyStats strategy, risk, execution, accounting, event_sink, end_to_end;
};

void initialize_aggregates(Aggregates& out, std::size_t ticks, std::size_t trials) {
    if (ticks > std::numeric_limits<std::size_t>::max() / trials) {
        throw std::overflow_error("benchmark aggregate tick count overflow");
    }
    const std::size_t aggregate_ticks = ticks * trials;
    out.strategy.initialize("strategy", aggregate_ticks, 1);
    out.risk.initialize("risk", aggregate_ticks, 1);
    out.execution.initialize("execution", aggregate_ticks, 2);
    out.accounting.initialize("accounting", aggregate_ticks, 1);
    out.event_sink.initialize("event_sink", aggregate_ticks, 4);
    out.end_to_end.initialize("end_to_end", aggregate_ticks, 1);
}

void append(llt::LatencyStats& target, const llt::LatencyStats& source) {
    for (const std::int64_t sample : source.samples()) {
        if (!target.record(sample)) throw std::overflow_error("benchmark aggregate collector overflow");
    }
}

void append(Aggregates& target, const llt::ReplayLatencies& source) {
    append(target.strategy, source.strategy); append(target.risk, source.risk);
    append(target.execution, source.execution); append(target.accounting, source.accounting);
    append(target.event_sink, source.event_sink); append(target.end_to_end, source.end_to_end);
}

void emit_csv_row(std::ostream& out, std::string_view mode, std::string_view collector,
                  const llt::LatencyStats& stats) {
    const llt::LatencySummary s = stats.summarize();
    out << mode << ',' << collector << ',' << s.count << ',' << stats.overflow_count() << ','
        << s.min_ns << ',' << s.mean_ns << ',' << s.p50_ns << ',';
    if (s.has_p99) out << s.p99_ns;
    out << ',';
    if (s.has_p99_9) out << s.p99_9_ns;
    out << ',';
    if (s.has_tail_mean_99) out << s.tail_mean_99_ns;
    out << ',' << s.max_ns << '\n';
}

bool run_mode(const std::vector<llt::MarketTick>& ticks, llt::ReplaySinkMode mode,
              const Options& options, std::ostream* csv, std::uint64_t& checksum_out) {
    const char* name = mode == llt::ReplaySinkMode::Sync ? "sync" : "async";
    const llt::ReplayConfig config{mode, llt::ExecutionStyle::Aggressive, 3000};
    for (std::size_t i = 0; i < options.warmup; ++i) {
        ChecksumSink sink;
        const llt::ReplayResult warmup = llt::run_replay(ticks, config, sink);
        if (warmup.sink_failed || warmup.latency_overflow || warmup.counters.dropped_events != 0) return false;
    }

    Aggregates aggregate;
    initialize_aggregates(aggregate, ticks.size(), options.measured);
    std::uint64_t reference_checksum = 0;
    for (std::size_t trial = 1; trial <= options.measured; ++trial) {
        ChecksumSink sink;
        const auto start = std::chrono::steady_clock::now();
        llt::ReplayResult result = llt::run_replay(ticks, config, sink);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        const bool valid = !result.sink_failed && !result.latency_overflow &&
            result.counters.dropped_events == 0 &&
            result.counters.venue_batch_overflows == 0 &&
            result.counters.persisted_events == result.counters.accepted_events;
        if (trial == 1) reference_checksum = sink.checksum();
        const bool deterministic_events = sink.checksum() == reference_checksum;
        std::cout << "trial mode=" << name << " index=" << trial << " elapsed_ns=" << elapsed
            << " accepted=" << result.counters.accepted_events
            << " persisted=" << result.counters.persisted_events
            << " dropped=" << result.counters.dropped_events
            << " latency_overflow=" << result.latency_overflow
            << " venue_batch_overflow=" << result.counters.venue_batch_overflows
            << " sink_events=" << sink.count()
            << " checksum=" << sink.checksum()
            << " status=" << (valid && deterministic_events ? "ok" : "failed") << '\n';
        if (!valid || !deterministic_events) return false;
        append(aggregate, result.latencies);
    }
    std::cout << "summary mode=" << name << " measurement=in-process_replay_latency\n";
    aggregate.strategy.print_summary(std::cout, "strategy");
    aggregate.risk.print_summary(std::cout, "risk");
    aggregate.execution.print_summary(std::cout, "execution");
    aggregate.accounting.print_summary(std::cout, "accounting");
    aggregate.event_sink.print_summary(std::cout, mode == llt::ReplaySinkMode::Sync ? "event_sink_sync" : "event_enqueue_async");
    aggregate.end_to_end.print_summary(std::cout, "end_to_end");
    if (csv != nullptr) {
        emit_csv_row(*csv, name, "strategy", aggregate.strategy);
        emit_csv_row(*csv, name, "risk", aggregate.risk);
        emit_csv_row(*csv, name, "execution", aggregate.execution);
        emit_csv_row(*csv, name, "accounting", aggregate.accounting);
        emit_csv_row(*csv, name, mode == llt::ReplaySinkMode::Sync ? "event_sink_sync" : "event_enqueue_async", aggregate.event_sink);
        emit_csv_row(*csv, name, "end_to_end", aggregate.end_to_end);
    }
    checksum_out = reference_checksum;
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const std::vector<llt::MarketTick> ticks = llt::CsvReader{}.read_all(options.input);
        std::ofstream csv;
        if (options.csv_path != nullptr) {
            csv.open(options.csv_path, std::ios::trunc);
            if (!csv) throw std::runtime_error("failed to open benchmark CSV output");
            csv << "mode,collector,count,overflow,min_ns,mean_ns,p50_ns,p99_ns,p99_9_ns,tail_mean_99_ns,max_ns\n";
        }
        std::cout << "benchmark=bench_replay measurement=in-process_replay_latency\n"
            << "build_type=" << LLT_BUILD_TYPE << " compiler=" << __VERSION__
            << " git_commit=" << LLT_GIT_COMMIT
            << " hardware_threads=" << std::thread::hardware_concurrency() << '\n'
            << "dataset=" << options.input << " dataset_fnv1a64=0x" << std::hex << file_hash(options.input)
            << std::dec << " ticks=" << ticks.size() << " warmup=" << options.warmup
            << " measured=" << options.measured << '\n'
            << "boundaries=module_call; end_to_end=tick_start_through_synchronous_business_and_event_submission; async_consumer_excluded\n";
        std::uint64_t sync_checksum = 0;
        std::uint64_t async_checksum = 0;
        const bool modes_valid = run_mode(ticks, llt::ReplaySinkMode::Sync, options,
            csv ? &csv : nullptr, sync_checksum) &&
            run_mode(ticks, llt::ReplaySinkMode::Async, options,
                csv ? &csv : nullptr, async_checksum);
        const bool side_channel_equivalent = modes_valid && sync_checksum == async_checksum;
        std::cout << "side_channel_equivalent=" << side_channel_equivalent
            << " sync_checksum=" << sync_checksum
            << " async_checksum=" << async_checksum << '\n';
        const bool ok = modes_valid && side_channel_equivalent;
        if (csv) { csv.flush(); if (!csv) throw std::runtime_error("failed to write benchmark CSV output"); }
        std::cout << "benchmark_status=" << (ok ? "ok" : "failed") << '\n';
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
