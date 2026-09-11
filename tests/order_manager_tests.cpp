#include <cstdlib>
#include <iostream>

#include "order_manager.h"

namespace {

bool check_true(const char* label, const bool value) {
    if (value) {
        return true;
    }
    std::cerr << label << " expected=true actual=false\n";
    return false;
}

bool check_eq(const char* label, const std::int64_t actual, const std::int64_t expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool check_state(const char* label, const llt::OrderState actual, const llt::OrderState expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << " state mismatch\n";
    return false;
}

bool check_outcome(
    const char* label,
    const llt::VenueEventOutcome actual,
    const llt::VenueEventOutcome expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << " expected=" << llt::venue_event_outcome_name(expected)
              << " actual=" << llt::venue_event_outcome_name(actual) << '\n';
    return false;
}

bool test_new_ack_partial_fill_cancel() {
    llt::OrderManager om;
    llt::Fill fill{};
    if (!check_true("submit_new", om.submit_new(101, llt::Side::Buy, 100000, 100))) {
        return false;
    }
    if (!check_state("pending_new", om.order().state, llt::OrderState::PendingNewAck)) {
        return false;
    }
    if (!check_outcome("new_ack", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::NewAck, 1000, 555, 0, 0}, fill),
        llt::VenueEventOutcome::Applied)) {
        return false;
    }
    if (!check_state("acked", om.order().state, llt::OrderState::Acked)) {
        return false;
    }
    if (!check_outcome("partial_fill", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::Fill, 1100, 555, 40, 99950}, fill),
        llt::VenueEventOutcome::Applied)) {
        return false;
    }
    if (!(check_state("partially_filled", om.order().state, llt::OrderState::PartiallyFilled) &&
        check_eq("leaves_after_partial", om.order().leaves_qty, 60) &&
        check_eq("fill_qty", fill.quantity, 40))) {
        return false;
    }
    if (!check_true("request_cancel", om.request_cancel())) {
        return false;
    }
    if (!check_state("pending_cancel", om.order().state, llt::OrderState::PendingCancel)) {
        return false;
    }
    if (!check_outcome("cancel_ack", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::CancelAck, 1200, 555, 0, 0}, fill),
        llt::VenueEventOutcome::Applied)) {
        return false;
    }
    return check_state("canceled", om.order().state, llt::OrderState::Canceled);
}

bool test_fill_to_complete() {
    llt::OrderManager om;
    llt::Fill fill{};
    if (!check_true("submit_new_full", om.submit_new(202, llt::Side::Sell, 101000, 30))) {
        return false;
    }
    if (!check_outcome("new_ack_full", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::NewAck, 2000, 777, 0, 0}, fill),
        llt::VenueEventOutcome::Applied)) {
        return false;
    }
    if (!check_outcome("final_fill", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::Fill, 2100, 777, 30, 101200}, fill),
        llt::VenueEventOutcome::Applied)) {
        return false;
    }
    return check_state("filled", om.order().state, llt::OrderState::Filled) &&
        check_eq("leaves_zero", om.order().leaves_qty, 0);
}

bool test_expired_lifecycle_and_defensive_rejection() {
    llt::OrderManager om;
    llt::Fill fill{};
    if (!check_true("expired.submit", om.submit_new(303, llt::Side::Buy, 100000, 100)) ||
        !check_outcome("expired.ack", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 3000, 888, 0, 0}, fill),
            llt::VenueEventOutcome::Applied) ||
        !check_outcome("expired.partial", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 3100, 888, 40, 100100}, fill),
            llt::VenueEventOutcome::Applied)) {
        return false;
    }

    const llt::ManagedOrder before_wrong_order = om.order();
    if (!check_outcome("VenueEventType::Expired wrong-order rejected", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Expired, 3200, 999, 0, 0}, fill),
            llt::VenueEventOutcome::WrongOrder) ||
        !check_state("wrong-order state unchanged", om.order().state, before_wrong_order.state) ||
        !check_eq("wrong-order leaves unchanged", om.order().leaves_qty, before_wrong_order.leaves_qty)) {
        return false;
    }

    if (!check_outcome("VenueEventType::Expired applied", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Expired, 3300, 888, 0, 0}, fill),
            llt::VenueEventOutcome::Applied) ||
        !check_state("OrderState::Expired", om.order().state, llt::OrderState::Expired) ||
        !check_eq("expired.cum preserved", om.order().cum_qty, 40) ||
        !check_eq("expired.leaves zero", om.order().leaves_qty, 0)) {
        return false;
    }

    if (!check_outcome("VenueEventType::Expired terminal rejected", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Expired, 3400, 888, 0, 0}, fill),
            llt::VenueEventOutcome::WrongState) ||
        !check_state("terminal remains OrderState::Expired", om.order().state, llt::OrderState::Expired)) {
        return false;
    }

    return check_true("submit after OrderState::Expired",
        om.submit_new(304, llt::Side::Sell, 100200, 10));
}

bool test_expired_rejected_outside_live_states() {
    const auto expired = llt::VenueEvent{llt::VenueEventType::Expired, 4000, 900, 0, 0};
    llt::Fill fill{};

    llt::OrderManager pre_ack;
    if (!pre_ack.submit_new(401, llt::Side::Buy, 100000, 10) ||
        pre_ack.on_venue_event(expired, fill) != llt::VenueEventOutcome::WrongState ||
        pre_ack.order().state != llt::OrderState::PendingNewAck) {
        return false;
    }

    llt::OrderManager filled;
    if (!filled.submit_new(402, llt::Side::Buy, 100000, 10) ||
        filled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 4000, 900, 0, 0}, fill) !=
            llt::VenueEventOutcome::Applied ||
        filled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 4100, 900, 10, 100000}, fill) !=
            llt::VenueEventOutcome::Applied ||
        filled.on_venue_event(expired, fill) != llt::VenueEventOutcome::WrongState ||
        filled.order().state != llt::OrderState::Filled) {
        return false;
    }

    llt::OrderManager canceled;
    if (!canceled.submit_new(403, llt::Side::Buy, 100000, 10) ||
        canceled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 4000, 900, 0, 0}, fill) !=
            llt::VenueEventOutcome::Applied ||
        !canceled.request_cancel() ||
        canceled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::CancelAck, 4100, 900, 0, 0}, fill) !=
            llt::VenueEventOutcome::Applied ||
        canceled.on_venue_event(expired, fill) != llt::VenueEventOutcome::WrongState ||
        canceled.order().state != llt::OrderState::Canceled) {
        return false;
    }

    return true;
}

bool test_event_outcomes_and_no_mutation() {
    llt::OrderManager om;
    llt::Fill fill{999, 999, 999, llt::Side::Sell};
    if (!om.submit_new(501, llt::Side::Buy, 100000, 100)) {
        return false;
    }

    const llt::ManagedOrder pending = om.order();
    const llt::VenueEventType pre_ack_types[]{
        llt::VenueEventType::Fill,
        llt::VenueEventType::CancelAck,
        llt::VenueEventType::CancelReject,
        llt::VenueEventType::Expired,
    };
    for (const llt::VenueEventType type : pre_ack_types) {
        if (!check_outcome("pre-ack event", om.on_venue_event(
                llt::VenueEvent{type, 5000, 700, 10, 100000}, fill),
                llt::VenueEventOutcome::WrongState) ||
            om.order().state != pending.state || om.order().leaves_qty != pending.leaves_qty ||
            fill.quantity != 0) {
            return false;
        }
    }
    if (!check_outcome("invalid NewAck id", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 5000, 0, 0, 0}, fill),
            llt::VenueEventOutcome::InvalidVenueOrderId) ||
        om.order().state != llt::OrderState::PendingNewAck) {
        return false;
    }
    if (!check_outcome("valid NewAck", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 5000, 700, 0, 0}, fill),
            llt::VenueEventOutcome::Applied)) {
        return false;
    }

    const llt::ManagedOrder acked = om.order();
    const llt::VenueEventType wrong_order_types[]{
        llt::VenueEventType::Fill,
        llt::VenueEventType::Expired,
    };
    for (const llt::VenueEventType type : wrong_order_types) {
        if (!check_outcome("wrong-order live event", om.on_venue_event(
                llt::VenueEvent{type, 5100, 701, 10, 100000}, fill),
                llt::VenueEventOutcome::WrongOrder) ||
            om.order().state != acked.state || om.order().cum_qty != acked.cum_qty ||
            om.order().leaves_qty != acked.leaves_qty || fill.quantity != 0) {
            return false;
        }
    }

    if (!check_outcome("zero fill", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 5200, 700, 0, 100000}, fill),
            llt::VenueEventOutcome::InvalidQuantity) ||
        !check_outcome("negative fill", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 5200, 700, -1, 100000}, fill),
            llt::VenueEventOutcome::InvalidQuantity) ||
        !check_outcome("overfill", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 5200, 700, 101, 100000}, fill),
            llt::VenueEventOutcome::InvalidQuantity) ||
        !check_outcome("invalid event id", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 5200, 0, 10, 100000}, fill),
            llt::VenueEventOutcome::InvalidVenueOrderId)) {
        return false;
    }

    if (!om.request_cancel()) {
        return false;
    }
    const llt::ManagedOrder pending_cancel = om.order();
    const llt::VenueEventType cancel_types[]{
        llt::VenueEventType::CancelAck,
        llt::VenueEventType::CancelReject,
    };
    for (const llt::VenueEventType type : cancel_types) {
        if (!check_outcome("wrong-order cancel event", om.on_venue_event(
                llt::VenueEvent{type, 5300, 701, 0, 0}, fill),
                llt::VenueEventOutcome::WrongOrder) ||
            om.order().state != pending_cancel.state ||
            om.order().leaves_qty != pending_cancel.leaves_qty) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const bool ok = test_new_ack_partial_fill_cancel() &&
        test_fill_to_complete() &&
        test_expired_lifecycle_and_defensive_rejection() &&
        test_expired_rejected_outside_live_states() &&
        test_event_outcomes_and_no_mutation();
    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "order_manager_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
