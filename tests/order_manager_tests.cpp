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

bool test_new_ack_partial_fill_cancel() {
    llt::OrderManager om;
    llt::Fill fill{};
    if (!check_true("submit_new", om.submit_new(101, llt::Side::Buy, 100000, 100))) {
        return false;
    }
    if (!check_state("pending_new", om.order().state, llt::OrderState::PendingNewAck)) {
        return false;
    }
    if (!check_true("new_ack", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::NewAck, 1000, 555, 0, 0}, fill))) {
        return false;
    }
    if (!check_state("acked", om.order().state, llt::OrderState::Acked)) {
        return false;
    }
    if (!check_true("partial_fill", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::Fill, 1100, 555, 40, 99950}, fill))) {
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
    if (!check_true("cancel_ack", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::CancelAck, 1200, 555, 0, 0}, fill))) {
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
    if (!check_true("new_ack_full", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::NewAck, 2000, 777, 0, 0}, fill))) {
        return false;
    }
    if (!check_true("final_fill", om.on_venue_event(
        llt::VenueEvent{llt::VenueEventType::Fill, 2100, 777, 30, 101200}, fill))) {
        return false;
    }
    return check_state("filled", om.order().state, llt::OrderState::Filled) &&
        check_eq("leaves_zero", om.order().leaves_qty, 0);
}

bool test_expired_lifecycle_and_defensive_rejection() {
    llt::OrderManager om;
    llt::Fill fill{};
    if (!check_true("expired.submit", om.submit_new(303, llt::Side::Buy, 100000, 100)) ||
        !check_true("expired.ack", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 3000, 888, 0, 0}, fill)) ||
        !check_true("expired.partial", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 3100, 888, 40, 100100}, fill))) {
        return false;
    }

    const llt::ManagedOrder before_wrong_order = om.order();
    if (!check_true("VenueEventType::Expired wrong-order rejected", !om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Expired, 3200, 999, 0, 0}, fill)) ||
        !check_state("wrong-order state unchanged", om.order().state, before_wrong_order.state) ||
        !check_eq("wrong-order leaves unchanged", om.order().leaves_qty, before_wrong_order.leaves_qty)) {
        return false;
    }

    if (!check_true("VenueEventType::Expired applied", om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Expired, 3300, 888, 0, 0}, fill)) ||
        !check_state("OrderState::Expired", om.order().state, llt::OrderState::Expired) ||
        !check_eq("expired.cum preserved", om.order().cum_qty, 40) ||
        !check_eq("expired.leaves zero", om.order().leaves_qty, 0)) {
        return false;
    }

    if (!check_true("VenueEventType::Expired terminal rejected", !om.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Expired, 3400, 888, 0, 0}, fill)) ||
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
        pre_ack.on_venue_event(expired, fill) ||
        pre_ack.order().state != llt::OrderState::PendingNewAck) {
        return false;
    }

    llt::OrderManager filled;
    if (!filled.submit_new(402, llt::Side::Buy, 100000, 10) ||
        !filled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 4000, 900, 0, 0}, fill) ||
        !filled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::Fill, 4100, 900, 10, 100000}, fill) ||
        filled.on_venue_event(expired, fill) ||
        filled.order().state != llt::OrderState::Filled) {
        return false;
    }

    llt::OrderManager canceled;
    if (!canceled.submit_new(403, llt::Side::Buy, 100000, 10) ||
        !canceled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 4000, 900, 0, 0}, fill) ||
        !canceled.request_cancel() ||
        !canceled.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::CancelAck, 4100, 900, 0, 0}, fill) ||
        canceled.on_venue_event(expired, fill) ||
        canceled.order().state != llt::OrderState::Canceled) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    const bool ok = test_new_ack_partial_fill_cancel() &&
        test_fill_to_complete() &&
        test_expired_lifecycle_and_defensive_rejection() &&
        test_expired_rejected_outside_live_states();
    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "order_manager_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
