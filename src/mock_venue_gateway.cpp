#include "mock_venue_gateway.h"

namespace llt {

bool MockVenueGateway::send_new(const GatewayNewOrder& request) noexcept {
    if (request.quantity <= 0 || pending_new_.active || active_order_.active) {
        return false;
    }

    pending_new_.active = true;
    pending_new_.request = request;
    pending_new_.ack_ready_ts_ns = request.submit_ts_ns + new_ack_latency_ns_;
    pending_new_.should_reject = (request.client_order_id % 3 == 0);
    return true;
}

bool MockVenueGateway::send_cancel(const TimestampNs request_ts_ns) noexcept {
    if (!active_order_.active || active_order_.cancel_sent) {
        return false;
    }

    active_order_.cancel_sent = true;
    active_order_.cancel_send_ts_ns = request_ts_ns + cancel_send_latency_ns_;
    return true;
}

VenueEventBatch MockVenueGateway::on_tick(const MarketTick& tick) noexcept {
    VenueEventBatch batch{};

    if (pending_new_.active && tick.receive_ts_ns >= pending_new_.ack_ready_ts_ns) {
        if (pending_new_.should_reject) {
            push_event(batch, VenueEvent{
                VenueEventType::NewReject,
                tick.receive_ts_ns,
                0,
                0,
                0,
            });
            pending_new_.active = false;
            return batch;
        }

        const std::int64_t venue_order_id = next_venue_order_id_++;
        active_order_ = ActiveOrder{
            true,
            pending_new_.request.client_order_id,
            venue_order_id,
            pending_new_.request.side,
            pending_new_.request.limit_price,
            pending_new_.request.quantity,
            false,
            0,
            false,
        };
        push_event(batch, VenueEvent{
            VenueEventType::NewAck,
            tick.receive_ts_ns,
            venue_order_id,
            0,
            0,
        });

        const OrderRequest order{
            pending_new_.request.side,
            pending_new_.request.quantity,
            pending_new_.request.limit_price,
        };
        const ExecutionReport report = execution_.submit_order(
            tick,
            order,
            pending_new_.request.style);
        active_order_.leaves_qty = report.leaves_qty;
        apply_execution_report(batch, report, tick.receive_ts_ns);
        if (pending_new_.request.style == ExecutionStyle::Aggressive &&
            active_order_.leaves_qty > 0) {
            push_event(batch, VenueEvent{
                VenueEventType::Expired,
                tick.receive_ts_ns,
                active_order_.venue_order_id,
                0,
                0,
            });
            active_order_.leaves_qty = 0;
        }
        pending_new_.active = false;
        close_active_order_if_done();
    }

    if (active_order_.active && active_order_.cancel_sent && tick.receive_ts_ns >= active_order_.cancel_send_ts_ns) {
        if (!active_order_.cancel_reject_once_done && (active_order_.venue_order_id % 2 == 1)) {
            push_event(batch, VenueEvent{
                VenueEventType::CancelReject,
                tick.receive_ts_ns,
                active_order_.venue_order_id,
                0,
                0,
            });
            active_order_.cancel_sent = false;
            active_order_.cancel_reject_once_done = true;
            return batch;
        }
        (void) execution_.request_cancel(tick.receive_ts_ns);
        active_order_.cancel_sent = false;
    }

    if (active_order_.active || execution_.has_resting_order()) {
        const ExecutionReport report = execution_.on_tick(tick);
        active_order_.leaves_qty = report.leaves_qty;
        apply_execution_report(batch, report, tick.receive_ts_ns);
        close_active_order_if_done();
    }

    return batch;
}

void MockVenueGateway::push_event(VenueEventBatch& batch, const VenueEvent& event) noexcept {
    if (batch.count >= batch.events.size()) {
        return;
    }
    batch.events[batch.count++] = event;
}

void MockVenueGateway::apply_execution_report(
    VenueEventBatch& batch,
    const ExecutionReport& report,
    const TimestampNs event_ts_ns) noexcept {
    if (report.has_fill && active_order_.active) {
        push_event(batch, VenueEvent{
            VenueEventType::Fill,
            event_ts_ns,
            active_order_.venue_order_id,
            report.fill.quantity,
            report.fill.price,
        });
    }

    if (report.cancel_acked && active_order_.active) {
        push_event(batch, VenueEvent{
            VenueEventType::CancelAck,
            event_ts_ns,
            active_order_.venue_order_id,
            0,
            0,
        });
    }
}

void MockVenueGateway::close_active_order_if_done() noexcept {
    if (!active_order_.active) {
        return;
    }

    if (active_order_.leaves_qty == 0 && !execution_.has_resting_order()) {
        active_order_.active = false;
        return;
    }
}

}  // namespace llt
