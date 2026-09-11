#include <cstdlib>
#include <chrono>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "accounting_engine.h"
#include "csv_reader.h"
#include "config_parser.h"
#include "fill_logger.h"
#include "layout_utils.h"
#include "latency_stats.h"
#include "mock_venue_gateway.h"
#include "order_manager.h"
#include "risk_engine.h"
#include "spsc_ring_buffer.h"
#include "strategy.h"

namespace {

enum class SinkMode {
    Sync = 0,
    Async = 1,
};

struct RuntimeConfig {
    SinkMode sink_mode;
    const char* persistence_path;
    llt::ExecutionStyle default_execution_style;
    llt::TimestampNs passive_cancel_after_ns;
};

std::string_view sink_mode_name(const SinkMode mode) {
    return mode == SinkMode::Sync ? "sync" : "async";
}

SinkMode parse_sink_mode(const char* value) {
    const std::string_view mode(value);
    if (mode == "sync") {
        return SinkMode::Sync;
    }
    if (mode == "async") {
        return SinkMode::Async;
    }

    throw std::invalid_argument("sink_mode is invalid; accepted values are sync|async");
}

llt::ExecutionStyle parse_execution_style(const char* value) {
    const std::string_view mode(value);
    if (mode == "aggressive") {
        return llt::ExecutionStyle::Aggressive;
    }
    if (mode == "passive") {
        return llt::ExecutionStyle::Passive;
    }

    throw std::invalid_argument("execution_style is invalid; accepted values are aggressive|passive");
}

RuntimeConfig parse_runtime_config(const int argc, char** argv) {
    RuntimeConfig config{
        SinkMode::Async,
        "build/event_journal.csv",
        llt::ExecutionStyle::Aggressive,
        3000,
    };

    if (argc >= 3) {
        config.sink_mode = parse_sink_mode(argv[2]);
    }
    if (argc >= 4) {
        config.persistence_path = argv[3];
    }
    if (argc >= 5) {
        config.default_execution_style = parse_execution_style(argv[4]);
    }
    if (argc >= 6) {
        config.passive_cancel_after_ns =
            llt::parse_nonnegative_timestamp_arg(argv[5], "passive_cancel_after_ns");
    }

    return config;
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

    std::ofstream persistence_out(config.persistence_path, std::ios::trunc);
    if (!persistence_out.is_open()) {
        std::cerr << "failed to open persistence path: " << config.persistence_path << '\n';
        return EXIT_FAILURE;
    }
    std::atomic<bool> persistence_failed{false};
    persistence_out << "event_kind,ts_ns,side,price,quantity,reject_reason\n";
    if (!persistence_out) {
        persistence_failed.store(true, std::memory_order_relaxed);
    }

    const std::uint32_t symbol_id = ticks.front().symbol_id;
    llt::AccountingEngine accounting;
    llt::MockVenueGateway gateway;
    llt::OrderManager order_manager;
    llt::RiskEngine risk(200);
    llt::Strategy strategy(100000, 120000, 50);
    llt::LatencyStats strategy_latency;
    llt::LatencyStats risk_latency;
    llt::LatencyStats execution_latency;
    llt::LatencyStats accounting_latency;
    llt::LatencyStats event_sink_latency;
    llt::LatencyStats end_to_end_latency;
    llt::SpscRingBuffer<llt::TradeEvent> event_queue(1024);
    std::atomic<bool> sink_done{false};
    std::atomic<std::int64_t> persisted_event_count{0};

    strategy_latency.reserve(ticks.size());
    risk_latency.reserve(ticks.size());
    execution_latency.reserve(ticks.size());
    accounting_latency.reserve(ticks.size());
    event_sink_latency.reserve(ticks.size());
    end_to_end_latency.reserve(ticks.size());

    std::thread sink_thread;
    if (config.sink_mode == SinkMode::Async) {
        // Console/file output is intentionally kept off the trading thread because I/O is
        // slow and jittery compared with the deterministic per-tick state transitions.
        sink_thread = std::thread([&event_queue, &sink_done, &persistence_out, &persisted_event_count, &persistence_failed]() {
            llt::TradeEvent event{};
            while (!sink_done.load(std::memory_order_acquire) || !event_queue.empty()) {
                if (event_queue.try_pop(event)) {
                    llt::write_trade_event_log(std::cout, event);
                    if (llt::write_trade_event_persistence(persistence_out, event)) {
                        persisted_event_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        persistence_failed.store(true, std::memory_order_relaxed);
                    }
                    continue;
                }

                std::this_thread::yield();
            }
        });
    }

    std::int64_t order_count = 0;
    std::int64_t reject_count = 0;
    std::int64_t fill_count = 0;
    std::int64_t dropped_async_events = 0;
    std::int64_t accepted_event_count = 0;
    std::int64_t rejected_venue_event_count = 0;
    std::int64_t wrong_state_event_count = 0;
    std::int64_t wrong_order_event_count = 0;
    std::int64_t invalid_quantity_event_count = 0;
    std::int64_t invalid_venue_order_id_event_count = 0;
    std::int64_t accounting_rejected_fill_count = 0;
    std::int64_t client_order_id_counter = 1;
    llt::TimestampNs passive_submit_ts_ns = -1;
    const auto emit_event = [&](const llt::TradeEvent& event) {
        ++accepted_event_count;
        const auto sink_start = std::chrono::steady_clock::now();
        if (config.sink_mode == SinkMode::Sync) {
            llt::write_trade_event_log(std::cout, event);
            if (llt::write_trade_event_persistence(persistence_out, event)) {
                persisted_event_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                persistence_failed.store(true, std::memory_order_relaxed);
            }
        } else if (!event_queue.try_push(event)) {
            ++dropped_async_events;
        }
        const auto sink_end = std::chrono::steady_clock::now();
        event_sink_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(sink_end - sink_start).count());
    };

    const auto handle_fill = [&](const llt::Fill& fill, const llt::MarketTick& tick) {
        if (fill.quantity <= 0) {
            return;
        }

        const auto accounting_start = std::chrono::steady_clock::now();
        if (!accounting.apply_fill(fill)) {
            ++accounting_rejected_fill_count;
            return;
        }
        accounting.mark_to_market((tick.bid_price + tick.ask_price) / 2);
        const auto accounting_end = std::chrono::steady_clock::now();
        accounting_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
            accounting_end - accounting_start).count());

        emit_event(llt::TradeEvent{
            fill.ts_ns,
            fill.price,
            fill.quantity,
            llt::EventKind::Fill,
            fill.side,
            llt::RejectReason::None,
        });
        ++fill_count;
    };

    const auto emit_oms_transition = [&](const llt::TimestampNs ts_ns, const llt::OrderState from, const llt::OrderState to, const llt::Side side) {
        if (from == to) {
            return;
        }
        emit_event(llt::TradeEvent{
            ts_ns,
            static_cast<llt::Price>(from),
            static_cast<std::int32_t>(to),
            llt::EventKind::OmsTransition,
            side,
            llt::RejectReason::None,
        });
    };

    const auto handle_venue_event = [&](const llt::VenueEvent& venue_event, const llt::Side fallback_side, const llt::MarketTick& tick) {
        const llt::OrderState from_state = order_manager.order().state;
        const llt::Side side = order_manager.order().state == llt::OrderState::Idle ? fallback_side : order_manager.order().side;
        llt::Fill venue_fill{};
        const llt::VenueEventOutcome outcome = order_manager.on_venue_event(venue_event, venue_fill);
        if (outcome == llt::VenueEventOutcome::Applied) {
            emit_oms_transition(venue_event.ts_ns, from_state, order_manager.order().state, side);
            handle_fill(venue_fill, tick);
            if (venue_event.type == llt::VenueEventType::NewReject) {
                emit_event(llt::TradeEvent{
                    venue_event.ts_ns,
                    0,
                    order_manager.order().order_qty,
                    llt::EventKind::VenueReject,
                    side,
                    llt::RejectReason::NewRejectedByVenue,
                });
            } else if (venue_event.type == llt::VenueEventType::CancelReject) {
                emit_event(llt::TradeEvent{
                    venue_event.ts_ns,
                    0,
                    order_manager.order().leaves_qty,
                    llt::EventKind::VenueReject,
                    side,
                    llt::RejectReason::CancelRejectedByVenue,
                });
            }
            return;
        }

        ++rejected_venue_event_count;
        switch (outcome) {
        case llt::VenueEventOutcome::WrongState:
            ++wrong_state_event_count;
            break;
        case llt::VenueEventOutcome::WrongOrder:
            ++wrong_order_event_count;
            break;
        case llt::VenueEventOutcome::InvalidQuantity:
            ++invalid_quantity_event_count;
            break;
        case llt::VenueEventOutcome::InvalidVenueOrderId:
            ++invalid_venue_order_id_event_count;
            break;
        case llt::VenueEventOutcome::Applied:
            break;
        }
    };

    for (const auto& tick : ticks) {
        // This loop is the hot path: every replayed tick flows through it, so anything we
        // put here directly contributes to end-to-end latency and tail jitter.
        const auto loop_start = std::chrono::steady_clock::now();
        if (tick.symbol_id != symbol_id) {
            sink_done.store(true, std::memory_order_release);
            if (sink_thread.joinable()) {
                sink_thread.join();
            }
            std::cerr << "multiple symbols are not supported in phase 1\n";
            return EXIT_FAILURE;
        }

        // Mark-to-market runs on every tick, so it remains part of the hot path.
        accounting.mark_to_market((tick.bid_price + tick.ask_price) / 2);

        // Resting passive orders are advanced on every market tick before new decisions are made.
        const auto resting_execution_start = std::chrono::steady_clock::now();
        const llt::VenueEventBatch batch = gateway.on_tick(tick);
        const auto resting_execution_end = std::chrono::steady_clock::now();
        execution_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
            resting_execution_end - resting_execution_start).count());
        for (std::size_t i = 0; i < batch.count; ++i) {
            handle_venue_event(batch.events[i], order_manager.order().side, tick);
        }

        if (order_manager.order().state == llt::OrderState::Filled ||
            order_manager.order().state == llt::OrderState::Canceled ||
            order_manager.order().state == llt::OrderState::Expired) {
            passive_submit_ts_ns = -1;
        }

        if (config.default_execution_style == llt::ExecutionStyle::Passive &&
            (order_manager.order().state == llt::OrderState::Acked ||
             order_manager.order().state == llt::OrderState::PartiallyFilled) &&
            passive_submit_ts_ns >= 0 &&
            tick.receive_ts_ns - passive_submit_ts_ns >= config.passive_cancel_after_ns) {
            const llt::OrderState from_state = order_manager.order().state;
            const llt::Side from_side = order_manager.order().side;
            if (llt::submit_cancel_request(gateway, order_manager, tick.receive_ts_ns)) {
                emit_oms_transition(tick.receive_ts_ns, from_state, llt::OrderState::PendingCancel, from_side);
            }
        }

        // Strategy evaluation is hot-path logic because the trading decision for this tick
        // cannot proceed until it finishes.
        const auto strategy_start = std::chrono::steady_clock::now();
        const auto decision = strategy.on_tick(tick);
        const auto strategy_end = std::chrono::steady_clock::now();
        strategy_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(strategy_end - strategy_start).count());
        if (!decision.has_order) {
            end_to_end_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - loop_start).count());
            continue;
        }

        ++order_count;
        // Risk is still hot-path logic: an order cannot be sent or simulated until it is
        // accepted, so this check stays synchronous.
        const auto risk_start = std::chrono::steady_clock::now();
        const auto risk_decision = risk.evaluate(accounting.position(), decision.order);
        if (!risk_decision.accepted) {
            const auto risk_end = std::chrono::steady_clock::now();
            risk_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(risk_end - risk_start).count());
            ++reject_count;
            const llt::TradeEvent reject_event{
                tick.receive_ts_ns,
                0,
                decision.order.quantity,
                llt::EventKind::RiskReject,
                decision.order.side,
                risk_decision.reject_reason,
            };
            emit_event(reject_event);
            end_to_end_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - loop_start).count());
            continue;
        }
        const auto risk_end = std::chrono::steady_clock::now();
        risk_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(risk_end - risk_start).count());

        // Execution simulation is on the hot path because fills determine immediate state
        // transitions and PnL updates for this tick.
        const auto execution_start = std::chrono::steady_clock::now();
        llt::OrderRequest request = decision.order;
        if (config.default_execution_style == llt::ExecutionStyle::Passive) {
            request.limit_price = request.side == llt::Side::Buy ? tick.bid_price : tick.ask_price;
        }
        const std::int64_t client_id = client_order_id_counter++;
        const bool accepted_local = order_manager.submit_new(client_id, request.side, request.limit_price, request.quantity);
        bool submit_report_accepted = false;
        if (accepted_local) {
            emit_oms_transition(tick.receive_ts_ns, llt::OrderState::Idle, llt::OrderState::PendingNewAck, request.side);
            submit_report_accepted = gateway.send_new(
                llt::GatewayNewOrder{
                    client_id,
                    request.side,
                    request.quantity,
                    request.limit_price,
                    config.default_execution_style,
                    tick.receive_ts_ns,
                });
            if (!submit_report_accepted) {
                handle_venue_event(
                    llt::VenueEvent{
                        llt::VenueEventType::NewReject,
                        tick.receive_ts_ns,
                        0,
                        0,
                        0,
                    },
                    request.side,
                    tick);
            }
        }
        const auto execution_end = std::chrono::steady_clock::now();
        execution_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
            execution_end - execution_start).count());
        if (config.default_execution_style == llt::ExecutionStyle::Passive &&
            submit_report_accepted) {
            passive_submit_ts_ns = tick.receive_ts_ns;
        }
        end_to_end_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - loop_start).count());
    }

    sink_done.store(true, std::memory_order_release);
    if (sink_thread.joinable()) {
        sink_thread.join();
    }
    persistence_out.flush();
    if (!persistence_out) {
        persistence_failed.store(true, std::memory_order_relaxed);
    }

    // Summary/reporting is cold-path work: it runs once after replay and is intentionally
    // kept out of the per-tick latency-sensitive trading flow.
    const auto& position = accounting.position();
    const auto& pnl = accounting.pnl();

    std::cout << "sink_mode=" << sink_mode_name(config.sink_mode) << '\n';
    std::cout << "persistence_path=" << config.persistence_path << '\n';
    std::cout << "execution_mode=" << (config.default_execution_style == llt::ExecutionStyle::Aggressive ? "aggressive" : "passive") << '\n';
    std::cout << "passive_cancel_after_ns=" << config.passive_cancel_after_ns << '\n';
    std::cout << "orders=" << order_count << '\n';
    std::cout << "rejects=" << reject_count << '\n';
    std::cout << "fills=" << fill_count << '\n';
    std::cout << "persisted_events=" << persisted_event_count.load(std::memory_order_relaxed) << '\n';
    std::cout << "dropped_async_events=" << dropped_async_events << '\n';
    std::cout << "events.accepted=" << accepted_event_count << '\n';
    std::cout << "events.persisted=" << persisted_event_count.load(std::memory_order_relaxed) << '\n';
    std::cout << "events.dropped=" << dropped_async_events << '\n';
    const bool async_drop_failed = dropped_async_events != 0;
    const bool persistence_write_failed = persistence_failed.load(std::memory_order_relaxed);
    const bool run_failed = async_drop_failed || persistence_write_failed;
    std::cout << "run_status=" << (run_failed ? "failed" : "ok") << '\n';
    std::cout << "failure_reasons=";
    if (!run_failed) {
        std::cout << "none";
    } else {
        if (async_drop_failed) {
            std::cout << "async_drop";
        }
        if (persistence_write_failed) {
            std::cout << (async_drop_failed ? "," : "") << "persist_write";
        }
    }
    std::cout << '\n';
    std::cout << "venue_events.rejected=" << rejected_venue_event_count << '\n';
    std::cout << "venue_events.wrong_state=" << wrong_state_event_count << '\n';
    std::cout << "venue_events.wrong_order=" << wrong_order_event_count << '\n';
    std::cout << "venue_events.invalid_quantity=" << invalid_quantity_event_count << '\n';
    std::cout << "venue_events.invalid_venue_order_id=" << invalid_venue_order_id_event_count << '\n';
    std::cout << "accounting.rejected_fills=" << accounting_rejected_fill_count << '\n';
    std::cout << "net_qty=" << position.net_qty << '\n';
    std::cout << "avg_price=" << position.avg_price << '\n';
    std::cout << "realized_pnl=" << pnl.realized_pnl << '\n';
    std::cout << "unrealized_pnl=" << pnl.unrealized_pnl << '\n';
    std::cout << "cash=" << pnl.cash << '\n';
    strategy_latency.print_summary(std::cout, "strategy");
    risk_latency.print_summary(std::cout, "risk");
    execution_latency.print_summary(std::cout, "execution");
    accounting_latency.print_summary(std::cout, "accounting");
    event_sink_latency.print_summary(std::cout, config.sink_mode == SinkMode::Sync ? "event_sink_sync" : "event_enqueue_async");
    end_to_end_latency.print_summary(std::cout, "end_to_end");
    const auto& execution_stats = gateway.execution_stats();
    std::cout << "execution_stats.aggressive_fills=" << execution_stats.aggressive_fill_count << '\n';
    std::cout << "execution_stats.passive_fills=" << execution_stats.passive_fill_count << '\n';
    std::cout << "execution_stats.partial_fills=" << execution_stats.partial_fill_count << '\n';
    std::cout << "execution_stats.cancel_requests=" << execution_stats.cancel_request_count << '\n';
    std::cout << "execution_stats.cancel_acks=" << execution_stats.cancel_ack_count << '\n';
    std::cout << "execution_stats.stale_fills=" << execution_stats.stale_fill_count << '\n';
    std::cout << "execution_stats.adverse_fills=" << execution_stats.adverse_fill_count << '\n';
    std::cout << "order_manager.state=" << static_cast<int>(order_manager.order().state) << '\n';
    std::cout << "order_manager.cum_qty=" << order_manager.order().cum_qty << '\n';
    std::cout << "order_manager.leaves_qty=" << order_manager.order().leaves_qty << '\n';
    llt::print_layout_summary(std::cout);

    return run_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
