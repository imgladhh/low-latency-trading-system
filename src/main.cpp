#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "config_parser.h"
#include "csv_reader.h"
#include "fill_logger.h"
#include "layout_utils.h"
#include "replay_runner.h"

namespace {
struct RuntimeConfig {
    llt::ReplaySinkMode sink_mode;
    const char* persistence_path;
    llt::ExecutionStyle execution_style;
    llt::TimestampNs passive_cancel_after_ns;
};

std::string_view sink_mode_name(llt::ReplaySinkMode mode) {
    return mode == llt::ReplaySinkMode::Sync ? "sync" : "async";
}
llt::ReplaySinkMode parse_sink_mode(const char* value) {
    const std::string_view mode(value);
    if (mode == "sync") return llt::ReplaySinkMode::Sync;
    if (mode == "async") return llt::ReplaySinkMode::Async;
    throw std::invalid_argument("sink_mode is invalid; accepted values are sync|async");
}
llt::ExecutionStyle parse_execution_style(const char* value) {
    const std::string_view mode(value);
    if (mode == "aggressive") return llt::ExecutionStyle::Aggressive;
    if (mode == "passive") return llt::ExecutionStyle::Passive;
    throw std::invalid_argument("execution_style is invalid; accepted values are aggressive|passive");
}
RuntimeConfig parse_runtime_config(int argc, char** argv) {
    RuntimeConfig config{llt::ReplaySinkMode::Async, "build/event_journal.csv",
        llt::ExecutionStyle::Aggressive, 3000};
    if (argc >= 3) config.sink_mode = parse_sink_mode(argv[2]);
    if (argc >= 4) config.persistence_path = argv[3];
    if (argc >= 5) config.execution_style = parse_execution_style(argv[4]);
    if (argc >= 6) config.passive_cancel_after_ns =
        llt::parse_nonnegative_timestamp_arg(argv[5], "passive_cancel_after_ns");
    return config;
}

class LoggingEventSink final : public llt::ReplayEventSink {
public:
    explicit LoggingEventSink(std::ostream& persistence) : persistence_(persistence) {}
    bool consume(const llt::TradeEvent& event) noexcept override {
        llt::write_trade_event_log(std::cout, event);
        return llt::write_trade_event_persistence(persistence_, event);
    }
private:
    std::ostream& persistence_;
};

void print_failure_reasons(bool async_drop, bool persistence, bool latency_overflow, bool batch_overflow) {
    if (!async_drop && !persistence && !latency_overflow && !batch_overflow) {
        std::cout << "none\n";
        return;
    }
    bool comma = false;
    const auto print = [&](const char* reason) {
        if (comma) std::cout << ',';
        std::cout << reason;
        comma = true;
    };
    if (async_drop) print("async_drop");
    if (persistence) print("persist_write");
    if (latency_overflow) print("latency_overflow");
    if (batch_overflow) print("venue_event_batch_overflow");
    std::cout << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: trading_main <csv-path> [sync|async] [persistence-path] [aggressive|passive] [passive-cancel-after-ns]\n";
        return EXIT_FAILURE;
    }
    RuntimeConfig config{};
    try {
        config = parse_runtime_config(argc, argv);
    } catch (const std::invalid_argument& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::vector<llt::MarketTick> ticks;
    try {
        ticks = llt::CsvReader{}.read_all(argv[1]);
    } catch (const llt::CsvReadError& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::ofstream persistence(config.persistence_path, std::ios::trunc);
    if (!persistence.is_open()) {
        std::cerr << "failed to open persistence path: " << config.persistence_path << '\n';
        return EXIT_FAILURE;
    }
    persistence << "event_kind,ts_ns,side,price,quantity,reject_reason\n";
    bool persistence_failed = !persistence;
    LoggingEventSink sink(persistence);
    llt::ReplayResult result{};
    try {
        result = llt::run_replay(ticks, llt::ReplayConfig{config.sink_mode,
            config.execution_style, config.passive_cancel_after_ns}, sink);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    persistence.flush();
    persistence_failed = persistence_failed || result.sink_failed || !persistence;
    const bool async_drop_failed = result.counters.dropped_events != 0;
    const bool batch_overflow_failed = result.counters.venue_batch_overflows != 0;
    const bool run_failed = async_drop_failed || persistence_failed || result.latency_overflow || batch_overflow_failed;

    std::cout << "sink_mode=" << sink_mode_name(config.sink_mode) << '\n';
    std::cout << "persistence_path=" << config.persistence_path << '\n';
    std::cout << "execution_mode=" << (config.execution_style == llt::ExecutionStyle::Aggressive ? "aggressive" : "passive") << '\n';
    std::cout << "passive_cancel_after_ns=" << config.passive_cancel_after_ns << '\n';
    std::cout << "strategy_signals=" << result.counters.strategy_signals << '\n';
    std::cout << "local_submissions=" << result.counters.local_submissions << '\n';
    std::cout << "gateway_submissions=" << result.counters.gateway_submissions << '\n';
    std::cout << "risk_rejects=" << result.counters.risk_rejects << '\n';
    std::cout << "venue_rejects=" << result.counters.venue_rejects << '\n';
    std::cout << "invalid_venue_events=" << result.counters.invalid_venue_events << '\n';
    std::cout << "fills=" << result.counters.fills << '\n';
    std::cout << "persisted_events=" << result.counters.persisted_events << '\n';
    std::cout << "dropped_async_events=" << result.counters.dropped_events << '\n';
    std::cout << "events.accepted=" << result.counters.accepted_events << '\n';
    std::cout << "events.persisted=" << result.counters.persisted_events << '\n';
    std::cout << "events.dropped=" << result.counters.dropped_events << '\n';
    std::cout << "run_status=" << (run_failed ? "failed" : "ok") << '\n';
    std::cout << "failure_reasons=";
    print_failure_reasons(async_drop_failed, persistence_failed, result.latency_overflow, batch_overflow_failed);
    std::cout << "venue_events.batch_overflow=" << result.counters.venue_batch_overflows << '\n';
    std::cout << "venue_events.wrong_state=" << result.counters.wrong_state_events << '\n';
    std::cout << "venue_events.wrong_order=" << result.counters.wrong_order_events << '\n';
    std::cout << "venue_events.invalid_quantity=" << result.counters.invalid_quantity_events << '\n';
    std::cout << "venue_events.invalid_venue_order_id=" << result.counters.invalid_venue_order_id_events << '\n';
    std::cout << "accounting.rejected_fills=" << result.counters.accounting_rejected_fills << '\n';
    std::cout << "net_qty=" << result.position.net_qty << '\n';
    std::cout << "avg_price=" << result.position.avg_price << '\n';
    std::cout << "realized_pnl=" << result.pnl.realized_pnl << '\n';
    std::cout << "unrealized_pnl=" << result.pnl.unrealized_pnl << '\n';
    std::cout << "cash=" << result.pnl.cash << '\n';
    result.latencies.strategy.print_summary(std::cout, "strategy");
    result.latencies.risk.print_summary(std::cout, "risk");
    result.latencies.execution.print_summary(std::cout, "execution");
    result.latencies.accounting.print_summary(std::cout, "accounting");
    result.latencies.event_sink.print_summary(std::cout,
        config.sink_mode == llt::ReplaySinkMode::Sync ? "event_sink_sync" : "event_enqueue_async");
    result.latencies.end_to_end.print_summary(std::cout, "end_to_end");
    const auto& stats = result.execution_stats;
    std::cout << "execution_stats.aggressive_fills=" << stats.aggressive_fill_count << '\n';
    std::cout << "execution_stats.passive_fills=" << stats.passive_fill_count << '\n';
    std::cout << "execution_stats.partial_fills=" << stats.partial_fill_count << '\n';
    std::cout << "execution_stats.cancel_requests=" << stats.cancel_request_count << '\n';
    std::cout << "execution_stats.cancel_acks=" << stats.cancel_ack_count << '\n';
    std::cout << "execution_stats.stale_fills=" << stats.stale_fill_count << '\n';
    std::cout << "execution_stats.adverse_fills=" << stats.adverse_fill_count << '\n';
    std::cout << "order_manager.state=" << static_cast<int>(result.order.state) << '\n';
    std::cout << "order_manager.cum_qty=" << result.order.cum_qty << '\n';
    std::cout << "order_manager.leaves_qty=" << result.order.leaves_qty << '\n';
    llt::print_layout_summary(std::cout);
    return run_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
