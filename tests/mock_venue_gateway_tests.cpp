#include <cstdlib>
#include <iostream>

#include "accounting_engine.h"
#include "mock_venue_gateway.h"

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

bool test_new_ack_and_fill_flow() {
    llt::MockVenueGateway gateway(200, 200);
    const bool sent = gateway.send_new(
        llt::GatewayNewOrder{1, llt::Side::Buy, 50, 100000, llt::ExecutionStyle::Passive, 1000});
    if (!check_true("send_new", sent)) {
        return false;
    }

    const llt::MarketTick tick1{1100, 1100, 100000, 100100, 1, 10, 10};
    const llt::VenueEventBatch batch1 = gateway.on_tick(tick1);
    if (!check_eq("batch1.count", batch1.count, 0)) {
        return false;
    }

    const llt::MarketTick tick2{1200, 1200, 100000, 100000, 1, 10, 20};
    const llt::VenueEventBatch batch2 = gateway.on_tick(tick2);
    if (!(check_eq("batch2.count", batch2.count, 2) &&
        check_true("batch2.new_ack", batch2.events[0].type == llt::VenueEventType::NewAck))) {
        return false;
    }
    if (!check_true("batch2.fill", batch2.events[1].type == llt::VenueEventType::Fill)) {
        return false;
    }

    const llt::MarketTick tick3{1300, 1300, 100000, 100000, 1, 10, 30};
    const llt::VenueEventBatch batch3 = gateway.on_tick(tick3);
    return check_eq("batch3.count", batch3.count, 1) &&
        check_true("batch3.fill", batch3.events[0].type == llt::VenueEventType::Fill);
}

bool test_cancel_ack_flow() {
    llt::MockVenueGateway gateway(200, 200);
    if (!check_true("cancel.send_new", gateway.send_new(
        llt::GatewayNewOrder{2, llt::Side::Buy, 40, 100000, llt::ExecutionStyle::Passive, 1000}))) {
        return false;
    }

    (void) gateway.on_tick(llt::MarketTick{1200, 1200, 100000, 100200, 1, 20, 20});
    if (!check_true("cancel.request", gateway.send_cancel(1300))) {
        return false;
    }

    const llt::VenueEventBatch before_ack =
        gateway.on_tick(llt::MarketTick{1400, 1400, 100000, 100200, 1, 20, 20});
    if (!check_eq("cancel.before_ack_count", before_ack.count, 0)) {
        return false;
    }

    const llt::VenueEventBatch after_ack =
        gateway.on_tick(llt::MarketTick{1500, 1500, 100000, 100200, 1, 20, 20});
    if (!check_eq("cancel.after_ack_count", after_ack.count, 0)) {
        return false;
    }

    const llt::VenueEventBatch final_ack =
        gateway.on_tick(llt::MarketTick{2500, 2500, 100000, 100200, 1, 20, 20});
    return check_eq("cancel.final_ack_count", final_ack.count, 1) &&
        check_true("cancel.ack_type", final_ack.events[0].type == llt::VenueEventType::CancelAck);
}

bool apply_batch(
    const llt::VenueEventBatch& batch,
    llt::OrderManager& oms,
    llt::AccountingEngine& accounting,
    std::int64_t& applied_fill_count) {
    for (std::size_t i = 0; i < batch.count; ++i) {
        llt::Fill fill{};
        if (oms.on_venue_event(batch.events[i], fill) != llt::VenueEventOutcome::Applied) {
            return false;
        }
        if (batch.events[i].type == llt::VenueEventType::Fill) {
            if (!accounting.apply_fill(fill)) {
                return false;
            }
            ++applied_fill_count;
        }
    }
    return true;
}

bool test_aggressive_ioc_lifecycles() {
    struct Scenario {
        const char* label;
        std::int64_t client_order_id;
        std::int32_t order_qty;
        std::int32_t available_qty;
        std::size_t expected_event_count;
        std::int32_t expected_cum_qty;
        llt::OrderState expected_state;
    };

    const Scenario scenarios[]{
        {"full", 10, 40, 50, 2, 40, llt::OrderState::Filled},
        {"partial", 11, 100, 40, 3, 40, llt::OrderState::Expired},
        {"zero", 13, 100, 0, 2, 0, llt::OrderState::Expired},
    };

    for (const Scenario& scenario : scenarios) {
        llt::MockVenueGateway gateway(0, 0);
        llt::OrderManager oms;
        llt::AccountingEngine accounting;
        if (!gateway.send_new(llt::GatewayNewOrder{
                scenario.client_order_id,
                llt::Side::Buy,
                scenario.order_qty,
                100200,
                llt::ExecutionStyle::Aggressive,
                1000}) ||
            !oms.submit_new(scenario.client_order_id, llt::Side::Buy, 100200, scenario.order_qty)) {
            std::cerr << scenario.label << " submit failed\n";
            return false;
        }

        const llt::VenueEventBatch batch = gateway.on_tick(
            llt::MarketTick{1000, 1000, 100000, 100200, 1, 50, scenario.available_qty});
        if (batch.count != scenario.expected_event_count ||
            batch.events[0].type != llt::VenueEventType::NewAck) {
            std::cerr << scenario.label << " event count/order mismatch\n";
            return false;
        }
        if (scenario.expected_cum_qty > 0 &&
            batch.events[1].type != llt::VenueEventType::Fill) {
            std::cerr << scenario.label << " missing VenueEventType::Fill\n";
            return false;
        }
        if (scenario.expected_state == llt::OrderState::Expired &&
            batch.events[batch.count - 1].type != llt::VenueEventType::Expired) {
            std::cerr << scenario.label << " missing VenueEventType::Expired\n";
            return false;
        }

        std::int64_t applied_fill_count = 0;
        if (!apply_batch(batch, oms, accounting, applied_fill_count) ||
            oms.order().state != scenario.expected_state ||
            oms.order().cum_qty != scenario.expected_cum_qty ||
            oms.order().leaves_qty != 0 ||
            accounting.position().net_qty != scenario.expected_cum_qty ||
            applied_fill_count != (scenario.expected_cum_qty > 0 ? 1 : 0)) {
            std::cerr << scenario.label << " cross-module state mismatch\n";
            return false;
        }

        if (!gateway.send_new(llt::GatewayNewOrder{
                scenario.client_order_id + 100,
                llt::Side::Sell,
                1,
                100000,
                llt::ExecutionStyle::Aggressive,
                2000}) ||
            !oms.submit_new(scenario.client_order_id + 100, llt::Side::Sell, 100000, 1)) {
            std::cerr << scenario.label << " resubmit failed\n";
            return false;
        }
    }

    return true;
}

bool test_cancel_submission_failure_does_not_strand_oms() {
    llt::MockVenueGateway inactive_gateway(0, 0);
    llt::OrderManager oms;
    llt::Fill fill{};
    if (!oms.submit_new(20, llt::Side::Buy, 100000, 10) ||
        oms.on_venue_event(
            llt::VenueEvent{llt::VenueEventType::NewAck, 1000, 777, 0, 0}, fill) !=
            llt::VenueEventOutcome::Applied) {
        return false;
    }

    if (llt::submit_cancel_request(inactive_gateway, oms, 1100)) {
        std::cerr << "inactive gateway unexpectedly accepted cancel\n";
        return false;
    }
    if (oms.order().state != llt::OrderState::Acked) {
        std::cerr << "failed gateway cancel stranded OMS\n";
        return false;
    }

    llt::MockVenueGateway active_gateway(0, 0);
    llt::OrderManager active_oms;
    if (!active_gateway.send_new(llt::GatewayNewOrder{
            22, llt::Side::Buy, 10, 100000, llt::ExecutionStyle::Passive, 2000}) ||
        !active_oms.submit_new(22, llt::Side::Buy, 100000, 10)) {
        return false;
    }
    const llt::VenueEventBatch ack = active_gateway.on_tick(
        llt::MarketTick{2000, 2000, 100000, 100200, 1, 10, 10});
    std::int64_t applied_fill_count = 0;
    llt::AccountingEngine accounting;
    if (!apply_batch(ack, active_oms, accounting, applied_fill_count) ||
        !llt::submit_cancel_request(active_gateway, active_oms, 2100)) {
        return false;
    }
    return active_oms.order().state == llt::OrderState::PendingCancel;
}

}  // namespace

int main() {
    const bool ok = test_new_ack_and_fill_flow() &&
        test_cancel_ack_flow() &&
        test_aggressive_ioc_lifecycles() &&
        test_cancel_submission_failure_does_not_strand_oms();
    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "mock_venue_gateway_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
