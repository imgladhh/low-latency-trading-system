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

VenueEventOutcome OrderManager::on_venue_event(
    const VenueEvent& event,
    Fill& fill_out) noexcept {
    fill_out = Fill{};

    switch (event.type) {
    case VenueEventType::NewAck:
        if (order_.state != OrderState::PendingNewAck) {
            return VenueEventOutcome::WrongState;
        }
        if (event.venue_order_id <= 0) {
            return VenueEventOutcome::InvalidVenueOrderId;
        }
        order_.venue_order_id = event.venue_order_id;
        order_.state = OrderState::Acked;
        return VenueEventOutcome::Applied;

    case VenueEventType::NewReject:
        if (order_.state != OrderState::PendingNewAck) {
            return VenueEventOutcome::WrongState;
        }
        order_.state = OrderState::Rejected;
        return VenueEventOutcome::Applied;

    case VenueEventType::Fill:
        if (order_.state != OrderState::Acked &&
            order_.state != OrderState::PartiallyFilled &&
            order_.state != OrderState::PendingCancel) {
            return VenueEventOutcome::WrongState;
        }
        if (event.venue_order_id <= 0) {
            return VenueEventOutcome::InvalidVenueOrderId;
        }
        if (event.venue_order_id != order_.venue_order_id) {
            return VenueEventOutcome::WrongOrder;
        }
        if (event.fill_qty <= 0 || event.fill_qty > order_.leaves_qty) {
            return VenueEventOutcome::InvalidQuantity;
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
        return VenueEventOutcome::Applied;

    case VenueEventType::CancelAck:
        if (order_.state != OrderState::PendingCancel) {
            return VenueEventOutcome::WrongState;
        }
        if (event.venue_order_id <= 0) {
            return VenueEventOutcome::InvalidVenueOrderId;
        }
        if (event.venue_order_id != order_.venue_order_id) {
            return VenueEventOutcome::WrongOrder;
        }
        order_.state = OrderState::Canceled;
        order_.leaves_qty = 0;
        return VenueEventOutcome::Applied;

    case VenueEventType::CancelReject:
        if (order_.state != OrderState::PendingCancel) {
            return VenueEventOutcome::WrongState;
        }
        if (event.venue_order_id <= 0) {
            return VenueEventOutcome::InvalidVenueOrderId;
        }
        if (event.venue_order_id != order_.venue_order_id) {
            return VenueEventOutcome::WrongOrder;
        }
        order_.state = order_.leaves_qty == order_.order_qty ? OrderState::Acked : OrderState::PartiallyFilled;
        return VenueEventOutcome::Applied;

    case VenueEventType::Expired:
        if (order_.state != OrderState::Acked &&
            order_.state != OrderState::PartiallyFilled) {
            return VenueEventOutcome::WrongState;
        }
        if (event.venue_order_id <= 0) {
            return VenueEventOutcome::InvalidVenueOrderId;
        }
        if (event.venue_order_id != order_.venue_order_id) {
            return VenueEventOutcome::WrongOrder;
        }
        order_.leaves_qty = 0;
        order_.state = OrderState::Expired;
        return VenueEventOutcome::Applied;
    }

    return VenueEventOutcome::WrongState;
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

const char* venue_event_outcome_name(const VenueEventOutcome outcome) noexcept {
    switch (outcome) {
    case VenueEventOutcome::Applied:
        return "Applied";
    case VenueEventOutcome::WrongState:
        return "WrongState";
    case VenueEventOutcome::WrongOrder:
        return "WrongOrder";
    case VenueEventOutcome::InvalidQuantity:
        return "InvalidQuantity";
    case VenueEventOutcome::InvalidVenueOrderId:
        return "InvalidVenueOrderId";
    }
    return "UnknownVenueEventOutcome";
}

}  // namespace llt
