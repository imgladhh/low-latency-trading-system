#include "replay_runner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "accounting_engine.h"
#include "mock_venue_gateway.h"
#include "risk_engine.h"
#include "spsc_ring_buffer.h"
#include "strategy.h"

namespace llt {

ReplayResult run_replay(
    const std::vector<MarketTick>& ticks,
    const ReplayConfig& config,
    ReplayEventSink& sink) {
    if (ticks.empty()) {
        throw std::invalid_argument("run_replay requires at least one tick");
    }

    ReplayResult result{};
    AccountingEngine accounting;
    MockVenueGateway gateway;
    OrderManager order_manager;
    RiskEngine risk(200);
    Strategy strategy(100000, 120000, 50);
    SpscRingBuffer<TradeEvent> event_queue(1024);
    std::atomic<bool> sink_done{false};
    std::atomic<bool> sink_failed{false};
    std::atomic<std::int64_t> persisted_events{0};

    result.latencies.strategy.initialize("strategy", ticks.size(), 1);
    result.latencies.risk.initialize("risk", ticks.size(), 1);
    result.latencies.execution.initialize("execution", ticks.size(), 2);
    result.latencies.accounting.initialize("accounting", ticks.size(), 1);
    result.latencies.event_sink.initialize("event_sink", ticks.size(), 4);
    result.latencies.end_to_end.initialize("end_to_end", ticks.size(), 1);

    std::thread sink_thread;
    if (config.sink_mode == ReplaySinkMode::Async) {
        sink_thread = std::thread([&]() {
            TradeEvent event{};
            while (!sink_done.load(std::memory_order_acquire) || !event_queue.empty()) {
                if (!event_queue.try_pop(event)) {
                    std::this_thread::yield();
                    continue;
                }
                if (sink.consume(event)) {
                    persisted_events.fetch_add(1, std::memory_order_relaxed);
                } else {
                    sink_failed.store(true, std::memory_order_relaxed);
                }
            }
        });
    }

    const auto emit_event = [&](const TradeEvent& event) {
        ++result.counters.accepted_events;
        const auto start = std::chrono::steady_clock::now();
        if (config.sink_mode == ReplaySinkMode::Sync) {
            if (sink.consume(event)) {
                persisted_events.fetch_add(1, std::memory_order_relaxed);
            } else {
                sink_failed.store(true, std::memory_order_relaxed);
            }
        } else if (!event_queue.try_push(event)) {
            ++result.counters.dropped_events;
        }
        const auto end = std::chrono::steady_clock::now();
        (void)result.latencies.event_sink.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    };

    const auto emit_transition = [&](TimestampNs ts, OrderState from, OrderState to, Side side) {
        if (from != to) {
            emit_event(TradeEvent{ts, static_cast<Price>(from), static_cast<std::int32_t>(to),
                EventKind::OmsTransition, side, RejectReason::None});
        }
    };

    const auto handle_fill = [&](const Fill& fill, const MarketTick& tick) {
        if (fill.quantity <= 0) {
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        if (!accounting.apply_fill(fill)) {
            ++result.counters.accounting_rejected_fills;
            return;
        }
        accounting.mark_to_market((tick.bid_price + tick.ask_price) / 2);
        const auto end = std::chrono::steady_clock::now();
        (void)result.latencies.accounting.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        emit_event(TradeEvent{fill.ts_ns, fill.price, fill.quantity, EventKind::Fill,
            fill.side, RejectReason::None});
        ++result.counters.fills;
    };

    const auto handle_venue_event = [&](const VenueEvent& event, Side fallback_side, const MarketTick& tick) {
        const OrderState from = order_manager.order().state;
        const Side side = from == OrderState::Idle ? fallback_side : order_manager.order().side;
        Fill fill{};
        const VenueEventOutcome outcome = order_manager.on_venue_event(event, fill);
        if (outcome == VenueEventOutcome::Applied) {
            emit_transition(event.ts_ns, from, order_manager.order().state, side);
            handle_fill(fill, tick);
            if (event.type == VenueEventType::NewReject) {
                ++result.counters.venue_rejects;
                emit_event(TradeEvent{event.ts_ns, 0, order_manager.order().order_qty,
                    EventKind::VenueReject, side, RejectReason::NewRejectedByVenue});
            } else if (event.type == VenueEventType::CancelReject) {
                ++result.counters.venue_rejects;
                emit_event(TradeEvent{event.ts_ns, 0, order_manager.order().leaves_qty,
                    EventKind::VenueReject, side, RejectReason::CancelRejectedByVenue});
            }
            return;
        }
        ++result.counters.invalid_venue_events;
        switch (outcome) {
        case VenueEventOutcome::WrongState: ++result.counters.wrong_state_events; break;
        case VenueEventOutcome::WrongOrder: ++result.counters.wrong_order_events; break;
        case VenueEventOutcome::InvalidQuantity: ++result.counters.invalid_quantity_events; break;
        case VenueEventOutcome::InvalidVenueOrderId: ++result.counters.invalid_venue_order_id_events; break;
        case VenueEventOutcome::Applied: break;
        }
    };

    const std::uint32_t symbol_id = ticks.front().symbol_id;
    std::int64_t client_order_id = 1;
    TimestampNs passive_submit_ts = -1;
    for (const MarketTick& tick : ticks) {
        if (tick.symbol_id != symbol_id) {
            sink_done.store(true, std::memory_order_release);
            if (sink_thread.joinable()) sink_thread.join();
            throw std::invalid_argument("run_replay supports exactly one symbol");
        }
        const auto loop_start = std::chrono::steady_clock::now();
        accounting.mark_to_market((tick.bid_price + tick.ask_price) / 2);

        const auto resting_start = std::chrono::steady_clock::now();
        const VenueEventBatch batch = gateway.on_tick(tick);
        result.counters.venue_batch_overflows += static_cast<std::int64_t>(batch.overflow_count);
        const auto resting_end = std::chrono::steady_clock::now();
        (void)result.latencies.execution.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(resting_end - resting_start).count());
        for (std::size_t i = 0; i < batch.count; ++i) {
            handle_venue_event(batch.events[i], order_manager.order().side, tick);
        }

        if (order_manager.order().state == OrderState::Filled ||
            order_manager.order().state == OrderState::Canceled ||
            order_manager.order().state == OrderState::Expired) {
            passive_submit_ts = -1;
        }
        if (config.execution_style == ExecutionStyle::Passive &&
            (order_manager.order().state == OrderState::Acked ||
             order_manager.order().state == OrderState::PartiallyFilled) &&
            passive_submit_ts >= 0 &&
            tick.receive_ts_ns - passive_submit_ts >= config.passive_cancel_after_ns) {
            const OrderState from = order_manager.order().state;
            const Side side = order_manager.order().side;
            if (submit_cancel_request(gateway, order_manager, tick.receive_ts_ns)) {
                emit_transition(tick.receive_ts_ns, from, OrderState::PendingCancel, side);
            }
        }

        const auto strategy_start = std::chrono::steady_clock::now();
        const StrategyDecision decision = strategy.on_tick(tick);
        const auto strategy_end = std::chrono::steady_clock::now();
        (void)result.latencies.strategy.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(strategy_end - strategy_start).count());
        if (!decision.has_order) {
            (void)result.latencies.end_to_end.record(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - loop_start).count());
            continue;
        }

        ++result.counters.strategy_signals;
        const auto risk_start = std::chrono::steady_clock::now();
        const OrderDecision risk_decision = risk.evaluate(accounting.position(), decision.order);
        const auto risk_end = std::chrono::steady_clock::now();
        (void)result.latencies.risk.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(risk_end - risk_start).count());
        if (!risk_decision.accepted) {
            ++result.counters.risk_rejects;
            emit_event(TradeEvent{tick.receive_ts_ns, 0, decision.order.quantity,
                EventKind::RiskReject, decision.order.side, risk_decision.reject_reason});
            (void)result.latencies.end_to_end.record(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - loop_start).count());
            continue;
        }

        const auto execution_start = std::chrono::steady_clock::now();
        OrderRequest request = decision.order;
        if (config.execution_style == ExecutionStyle::Passive) {
            request.limit_price = request.side == Side::Buy ? tick.bid_price : tick.ask_price;
        }
        const std::int64_t current_client_id = client_order_id++;
        const bool local = order_manager.submit_new(
            current_client_id, request.side, request.limit_price, request.quantity);
        bool gateway_accepted = false;
        if (local) {
            ++result.counters.local_submissions;
            emit_transition(tick.receive_ts_ns, OrderState::Idle, OrderState::PendingNewAck, request.side);
            ++result.counters.gateway_submissions;
            gateway_accepted = gateway.send_new(GatewayNewOrder{current_client_id, request.side,
                request.quantity, request.limit_price, config.execution_style, tick.receive_ts_ns});
            if (!gateway_accepted) {
                handle_venue_event(VenueEvent{VenueEventType::NewReject, tick.receive_ts_ns, 0, 0, 0},
                    request.side, tick);
            }
        }
        const auto execution_end = std::chrono::steady_clock::now();
        (void)result.latencies.execution.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(execution_end - execution_start).count());
        if (config.execution_style == ExecutionStyle::Passive && gateway_accepted) {
            passive_submit_ts = tick.receive_ts_ns;
        }
        (void)result.latencies.end_to_end.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - loop_start).count());
    }

    sink_done.store(true, std::memory_order_release);
    if (sink_thread.joinable()) sink_thread.join();
    result.counters.persisted_events = persisted_events.load(std::memory_order_relaxed);
    result.position = accounting.position();
    result.pnl = accounting.pnl();
    result.order = order_manager.order();
    result.execution_stats = gateway.execution_stats();
    result.sink_failed = sink_failed.load(std::memory_order_relaxed);
    const std::array<const LatencyStats*, 6> collectors{
        &result.latencies.strategy, &result.latencies.risk, &result.latencies.execution,
        &result.latencies.accounting, &result.latencies.event_sink, &result.latencies.end_to_end};
    result.latency_overflow = std::any_of(collectors.begin(), collectors.end(),
        [](const LatencyStats* stats) { return stats->overflow_count() != 0; });
    return result;
}

}  // namespace llt
