#include "execution_simulator.h"

namespace llt {

ExecutionReport ExecutionSimulator::submit_order(
    const MarketTick& tick,
    const OrderRequest& request,
    const ExecutionStyle style) noexcept {
    if (request.quantity <= 0) {
        return ExecutionReport{false, false, false, false, false, false, Fill{}, 0};
    }

    if (style == ExecutionStyle::Aggressive) {
        return aggressive_fill(tick, request);
    }

    if (resting_order_.active) {
        return ExecutionReport{false, false, false, false, false, false, Fill{}, resting_order_.leaves_qty};
    }

    resting_order_ = RestingOrder{
        true,
        request.side,
        request.limit_price,
        request.quantity,
        request.side == Side::Buy ? tick.bid_qty : tick.ask_qty,
        tick.receive_ts_ns,
        false,
        0,
    };

    return ExecutionReport{true, false, false, false, false, false, Fill{}, resting_order_.leaves_qty};
}

ExecutionReport ExecutionSimulator::on_tick(const MarketTick& tick) noexcept {
    if (!resting_order_.active) {
        return ExecutionReport{true, false, false, false, false, false, Fill{}, 0};
    }

    const ExecutionReport fill_report = passive_fill_from_market(tick);
    if (fill_report.has_fill) {
        return fill_report;
    }

    if (resting_order_.active &&
        resting_order_.cancel_pending &&
        tick.receive_ts_ns >= resting_order_.cancel_effective_ts_ns) {
        resting_order_.active = false;
        resting_order_.cancel_pending = false;
        ++stats_.cancel_ack_count;
        return ExecutionReport{true, false, false, true, false, false, Fill{}, 0};
    }

    return ExecutionReport{true, false, false, false, false, false, Fill{}, resting_order_.active ? resting_order_.leaves_qty : 0};
}

bool ExecutionSimulator::request_cancel(const TimestampNs ts_ns) noexcept {
    if (!resting_order_.active || resting_order_.cancel_pending) {
        return false;
    }

    resting_order_.cancel_pending = true;
    resting_order_.cancel_effective_ts_ns = ts_ns + cancel_latency_ns_;
    ++stats_.cancel_request_count;
    return true;
}

ExecutionReport ExecutionSimulator::aggressive_fill(
    const MarketTick& tick,
    const OrderRequest& request) noexcept {
    const std::int32_t available_qty = request.side == Side::Buy ? tick.ask_qty : tick.bid_qty;
    const std::int32_t fill_qty =
        available_qty < request.quantity ? available_qty : request.quantity;
    if (fill_qty <= 0) {
        return ExecutionReport{true, false, false, false, false, false, Fill{}, request.quantity};
    }

    const bool was_partial = fill_qty < request.quantity;
    if (was_partial) {
        ++stats_.partial_fill_count;
    }
    ++stats_.aggressive_fill_count;

    const Fill fill{
        tick.receive_ts_ns,
        request.side == Side::Buy ? tick.ask_price : tick.bid_price,
        fill_qty,
        request.side,
    };

    return ExecutionReport{
        true,
        true,
        was_partial,
        false,
        false,
        false,
        fill,
        request.quantity - fill_qty,
    };
}

ExecutionReport ExecutionSimulator::passive_fill_from_market(const MarketTick& tick) noexcept {
    if (!is_marketable(tick)) {
        return ExecutionReport{true, false, false, false, false, false, Fill{}, resting_order_.leaves_qty};
    }

    std::int32_t available_qty = visible_opposite_qty(tick);
    if (resting_order_.queue_ahead_qty > 0) {
        const std::int32_t queue_consumed =
            resting_order_.queue_ahead_qty < available_qty ? resting_order_.queue_ahead_qty : available_qty;
        resting_order_.queue_ahead_qty -= queue_consumed;
        available_qty -= queue_consumed;
    }

    if (available_qty <= 0) {
        return ExecutionReport{true, false, false, false, false, false, Fill{}, resting_order_.leaves_qty};
    }

    const std::int32_t fill_qty =
        resting_order_.leaves_qty < available_qty ? resting_order_.leaves_qty : available_qty;
    const bool was_partial = fill_qty < resting_order_.leaves_qty;
    resting_order_.leaves_qty -= fill_qty;

    const bool stale_quote = tick.receive_ts_ns - resting_order_.submit_ts_ns > stale_after_ns_;
    const bool adverse_selection = detect_adverse_selection(tick);
    if (was_partial) {
        ++stats_.partial_fill_count;
    }
    if (stale_quote) {
        ++stats_.stale_fill_count;
    }
    if (adverse_selection) {
        ++stats_.adverse_fill_count;
    }
    ++stats_.passive_fill_count;

    const Fill fill{
        tick.receive_ts_ns,
        resting_order_.price,
        fill_qty,
        resting_order_.side,
    };
    const std::int32_t leaves_qty = resting_order_.leaves_qty;
    if (resting_order_.leaves_qty == 0) {
        resting_order_.active = false;
        resting_order_.cancel_pending = false;
    }

    return ExecutionReport{
        true,
        true,
        was_partial,
        false,
        stale_quote,
        adverse_selection,
        fill,
        leaves_qty,
    };
}

bool ExecutionSimulator::is_marketable(const MarketTick& tick) const noexcept {
    if (!resting_order_.active) {
        return false;
    }

    if (resting_order_.side == Side::Buy) {
        return tick.ask_price <= resting_order_.price;
    }

    return tick.bid_price >= resting_order_.price;
}

std::int32_t ExecutionSimulator::visible_opposite_qty(const MarketTick& tick) const noexcept {
    return resting_order_.side == Side::Buy ? tick.ask_qty : tick.bid_qty;
}

bool ExecutionSimulator::detect_adverse_selection(const MarketTick& tick) const noexcept {
    const Price mid_price = (tick.bid_price + tick.ask_price) / 2;
    if (resting_order_.side == Side::Buy) {
        return mid_price + adverse_move_threshold_ <= resting_order_.price;
    }

    return mid_price - adverse_move_threshold_ >= resting_order_.price;
}

}  // namespace llt
