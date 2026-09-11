#include <cstdlib>
#include <iostream>

#include "execution_simulator.h"

namespace {

bool check_true(const char* label, const bool value) {
    if (value) {
        return true;
    }

    std::cerr << label << " expected=true actual=false\n";
    return false;
}

bool check_false(const char* label, const bool value) {
    if (!value) {
        return true;
    }

    std::cerr << label << " expected=false actual=true\n";
    return false;
}

bool check_eq(const char* label, const std::int64_t actual, const std::int64_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool test_aggressive_partial_fill() {
    llt::ExecutionSimulator sim;
    const llt::MarketTick tick{1000, 1000, 100000, 100200, 1, 50, 40};
    const llt::ExecutionReport report =
        sim.submit_order(tick, llt::OrderRequest{llt::Side::Buy, 100, 0}, llt::ExecutionStyle::Aggressive);

    return check_true("aggr.accepted", report.accepted) &&
        check_true("aggr.fill", report.has_fill) &&
        check_true("aggr.partial", report.was_partial) &&
        check_eq("aggr.fill_qty", report.fill.quantity, 40) &&
        check_eq("aggr.fill_price", report.fill.price, 100200) &&
        check_eq("aggr.leaves", report.leaves_qty, 60);
}

bool test_aggressive_full_and_zero_fill() {
    llt::ExecutionSimulator sim;
    const llt::ExecutionReport full = sim.submit_order(
        llt::MarketTick{1000, 1000, 100000, 100200, 1, 50, 100},
        llt::OrderRequest{llt::Side::Buy, 100, 0},
        llt::ExecutionStyle::Aggressive);
    const llt::ExecutionReport zero = sim.submit_order(
        llt::MarketTick{2000, 2000, 100000, 100200, 1, 50, 0},
        llt::OrderRequest{llt::Side::Buy, 100, 0},
        llt::ExecutionStyle::Aggressive);

    return check_true("aggr.full.fill", full.has_fill) &&
        check_false("aggr.full.partial", full.was_partial) &&
        check_eq("aggr.full.leaves", full.leaves_qty, 0) &&
        check_false("aggr.zero.fill", zero.has_fill) &&
        check_eq("aggr.zero.leaves", zero.leaves_qty, 100);
}

bool test_passive_queue_progression() {
    llt::ExecutionSimulator sim;
    const llt::MarketTick submit_tick{1000, 1000, 100000, 100200, 1, 50, 40};
    const llt::ExecutionReport accepted =
        sim.submit_order(submit_tick, llt::OrderRequest{llt::Side::Buy, 60, 100000}, llt::ExecutionStyle::Passive);
    if (!(check_true("passive.accepted", accepted.accepted) &&
        check_false("passive.fill_on_submit", accepted.has_fill))) {
        return false;
    }

    const llt::MarketTick tick2{2000, 2000, 99900, 100000, 1, 20, 40};
    const llt::ExecutionReport report2 = sim.on_tick(tick2);
    if (!(check_false("passive.tick2.fill", report2.has_fill) &&
        check_eq("passive.tick2.leaves", report2.leaves_qty, 60))) {
        return false;
    }

    const llt::MarketTick tick3{3000, 3000, 99800, 100000, 1, 20, 40};
    const llt::ExecutionReport report3 = sim.on_tick(tick3);
    if (!(check_true("passive.tick3.fill", report3.has_fill) &&
        check_true("passive.tick3.partial", report3.was_partial) &&
        check_eq("passive.tick3.fill_qty", report3.fill.quantity, 30) &&
        check_eq("passive.tick3.leaves", report3.leaves_qty, 30))) {
        return false;
    }

    const llt::MarketTick tick4{4000, 4000, 99700, 100000, 1, 20, 50};
    const llt::ExecutionReport report4 = sim.on_tick(tick4);
    return check_true("passive.tick4.fill", report4.has_fill) &&
        check_eq("passive.tick4.fill_qty", report4.fill.quantity, 30) &&
        check_eq("passive.tick4.leaves", report4.leaves_qty, 0) &&
        check_false("passive.active_done", sim.has_resting_order());
}

bool test_cancel_latency_ack() {
    llt::ExecutionSimulator sim(1000, 1000, llt::PRICE_SCALE);
    const llt::MarketTick submit_tick{1000, 1000, 100000, 100300, 1, 10, 10};
    const llt::ExecutionReport accepted =
        sim.submit_order(submit_tick, llt::OrderRequest{llt::Side::Buy, 25, 100000}, llt::ExecutionStyle::Passive);
    if (!check_true("cancel.accepted", accepted.accepted)) {
        return false;
    }

    if (!check_true("cancel.request", sim.request_cancel(1500))) {
        return false;
    }

    const llt::MarketTick before_cancel{2000, 2000, 100100, 100300, 1, 10, 10};
    const llt::ExecutionReport report_before = sim.on_tick(before_cancel);
    if (!(check_false("cancel.before_ack", report_before.cancel_acked) &&
        check_true("cancel.still_active", sim.has_resting_order()))) {
        return false;
    }

    const llt::MarketTick after_cancel{2600, 2600, 100100, 100300, 1, 10, 10};
    const llt::ExecutionReport report_after = sim.on_tick(after_cancel);
    return check_true("cancel.acked", report_after.cancel_acked) &&
        check_false("cancel.active_done", sim.has_resting_order());
}

bool test_stale_and_adverse_selection() {
    llt::ExecutionSimulator sim(1000, 1000, 200);
    const llt::MarketTick submit_tick{1000, 1000, 100000, 100300, 1, 5, 0};
    const llt::ExecutionReport accepted =
        sim.submit_order(submit_tick, llt::OrderRequest{llt::Side::Buy, 10, 100000}, llt::ExecutionStyle::Passive);
    if (!check_true("stale.accepted", accepted.accepted)) {
        return false;
    }

    const llt::MarketTick fill_tick{2500, 2500, 99600, 100000, 1, 5, 20};
    const llt::ExecutionReport report = sim.on_tick(fill_tick);
    return check_true("stale.fill", report.has_fill) &&
        check_true("stale.flag", report.stale_quote) &&
        check_true("adverse.flag", report.adverse_selection);
}

}  // namespace

int main() {
    const bool ok =
        test_aggressive_partial_fill() &&
        test_aggressive_full_and_zero_fill() &&
        test_passive_queue_progression() &&
        test_cancel_latency_ack() &&
        test_stale_and_adverse_selection();

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "execution_simulator_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
