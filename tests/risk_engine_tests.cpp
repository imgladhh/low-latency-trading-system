#include <cstdlib>
#include <iostream>

#include "risk_engine.h"

namespace {

bool test_quantity_boundary() {
    const llt::RiskEngine risk(100);
    const llt::PositionState flat{0, 0};
    const llt::OrderDecision zero = risk.evaluate(
        flat, llt::OrderRequest{llt::Side::Buy, 0, 100000});
    const llt::OrderDecision negative = risk.evaluate(
        flat, llt::OrderRequest{llt::Side::Sell, -1, 100000});
    return !zero.accepted && zero.reject_reason == llt::RejectReason::InvalidQuantity &&
        !negative.accepted && negative.reject_reason == llt::RejectReason::InvalidQuantity;
}

bool test_position_boundary() {
    const llt::RiskEngine risk(100);
    const llt::OrderDecision accepted = risk.evaluate(
        llt::PositionState{50, 100000},
        llt::OrderRequest{llt::Side::Buy, 50, 100000});
    const llt::OrderDecision rejected = risk.evaluate(
        llt::PositionState{50, 100000},
        llt::OrderRequest{llt::Side::Buy, 51, 100000});
    return accepted.accepted && accepted.reject_reason == llt::RejectReason::None &&
        !rejected.accepted && rejected.reject_reason == llt::RejectReason::MaxPosition;
}

}  // namespace

int main() {
    if (!test_quantity_boundary() || !test_position_boundary()) {
        std::cerr << "risk_engine_tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "risk_engine_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
