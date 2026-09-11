#include "order_manager.h"

namespace llt {

bool OrderManager::submit_new(
    const std::int64_t client_order_id,
    const Side side,
    const Price limit_price,
    const std::int32_t qty) noexcept {
    if (order_.state == OrderState::Filled ||
        order_.state == OrderState::Canceled ||
        order_.state == OrderState::Rejected ||
        order_.state == OrderState::Expired) {
        order_ = ManagedOrder{0, 0, Side::Buy, 0, 0, 0, 0, OrderState::Idle};
    }

    if (qty <= 0 || order_.state != OrderState::Idle) {
        return false;
    }

    order_ = ManagedOrder{
        client_order_id,
        0,
        side,
        limit_price,
        qty,
        0,
        qty,
        OrderState::PendingNewAck,
    };
    return true;
}

bool OrderManager::request_cancel() noexcept {
    if (order_.state != OrderState::Acked && order_.state != OrderState::PartiallyFilled) {
        return false;
    }

    order_.state = OrderState::PendingCancel;
    return true;
}

bool OrderManager::on_venue_event(const VenueEvent& event, Fill& fill_out) noexcept {
    fill_out = Fill{};

    switch (event.type) {
    case VenueEventType::NewAck:
        if (order_.state != OrderState::PendingNewAck) {
            return false;
        }
        order_.venue_order_id = event.venue_order_id;
        order_.state = OrderState::Acked;
        return true;

    case VenueEventType::NewReject:
        if (order_.state != OrderState::PendingNewAck) {
            return false;
        }
        order_.state = OrderState::Rejected;
        return true;

    case VenueEventType::Fill:
        if (order_.state != OrderState::Acked &&
            order_.state != OrderState::PartiallyFilled &&
            order_.state != OrderState::PendingCancel) {
            return false;
        }
        if (event.fill_qty <= 0 || event.fill_qty > order_.leaves_qty) {
            return false;
        }
        order_.cum_qty += event.fill_qty;
        order_.leaves_qty -= event.fill_qty;
        fill_out = Fill{
            event.ts_ns,
            event.fill_price,
            event.fill_qty,
            order_.side,
        };
        order_.state = order_.leaves_qty == 0 ? OrderState::Filled : OrderState::PartiallyFilled;
        return true;

    case VenueEventType::CancelAck:
        if (order_.state != OrderState::PendingCancel) {
            return false;
        }
        order_.state = OrderState::Canceled;
        return true;

    case VenueEventType::CancelReject:
        if (order_.state != OrderState::PendingCancel) {
            return false;
        }
        order_.state = order_.leaves_qty == order_.order_qty ? OrderState::Acked : OrderState::PartiallyFilled;
        return true;

    case VenueEventType::Expired:
        if ((order_.state != OrderState::Acked &&
             order_.state != OrderState::PartiallyFilled) ||
            event.venue_order_id != order_.venue_order_id) {
            return false;
        }
        order_.leaves_qty = 0;
        order_.state = OrderState::Expired;
        return true;
    }

    return false;
}

const char* order_state_name(const OrderState state) noexcept {
    switch (state) {
    case OrderState::Idle:
        return "Idle";
    case OrderState::PendingNewAck:
        return "PendingNewAck";
    case OrderState::Acked:
        return "Acked";
    case OrderState::PartiallyFilled:
        return "PartiallyFilled";
    case OrderState::PendingCancel:
        return "PendingCancel";
    case OrderState::Filled:
        return "Filled";
    case OrderState::Canceled:
        return "Canceled";
    case OrderState::Rejected:
        return "Rejected";
    case OrderState::Expired:
        return "Expired";
    }

    return "Unknown";
}

}  // namespace llt
