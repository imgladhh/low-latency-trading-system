#include <cstdlib>
#include <iostream>
#include <random>

#include "accounting_engine.h"

namespace {

using llt::AccountingEngine;
using llt::Fill;
using llt::Price;
using llt::Side;

constexpr Price px(const std::int64_t whole) {
    return whole * llt::PRICE_SCALE;
}

bool check_eq(const char* label, const std::int64_t actual, const std::int64_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool test_basic_open() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, px(10), 100, Side::Buy})) {
        return false;
    }

    return check_eq("basic_open.net_qty", engine.position().net_qty, 100) &&
        check_eq("basic_open.avg_price", engine.position().avg_price, px(10)) &&
        check_eq("basic_open.realized", engine.pnl().realized_pnl, 0);
}

bool test_same_side_add() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, px(10), 100, Side::Buy}) ||
        !engine.apply_fill(Fill{2, px(12), 100, Side::Buy})) {
        return false;
    }

    return check_eq("same_side_add.net_qty", engine.position().net_qty, 200) &&
        check_eq("same_side_add.avg_price", engine.position().avg_price, px(11));
}

bool test_partial_close() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, px(10), 100, Side::Buy}) ||
        !engine.apply_fill(Fill{2, px(15), 50, Side::Sell})) {
        return false;
    }

    return check_eq("partial_close.net_qty", engine.position().net_qty, 50) &&
        check_eq("partial_close.avg_price", engine.position().avg_price, px(10)) &&
        check_eq("partial_close.realized", engine.pnl().realized_pnl, px(5) * 50);
}

bool test_full_close() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, px(10), 100, Side::Buy}) ||
        !engine.apply_fill(Fill{2, px(8), 100, Side::Sell})) {
        return false;
    }

    return check_eq("full_close.net_qty", engine.position().net_qty, 0) &&
        check_eq("full_close.avg_price", engine.position().avg_price, 0) &&
        check_eq("full_close.realized", engine.pnl().realized_pnl, -px(2) * 100);
}

bool test_flip_position() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, px(10), 100, Side::Buy}) ||
        !engine.apply_fill(Fill{2, px(12), 150, Side::Sell})) {
        return false;
    }

    return check_eq("flip_position.net_qty", engine.position().net_qty, -50) &&
        check_eq("flip_position.avg_price", engine.position().avg_price, px(12)) &&
        check_eq("flip_position.realized", engine.pnl().realized_pnl, px(2) * 100);
}

bool test_short_cover_partial() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, px(10), 100, Side::Sell}) ||
        !engine.apply_fill(Fill{2, px(8), 50, Side::Buy})) {
        return false;
    }

    return check_eq("short_cover_partial.net_qty", engine.position().net_qty, -50) &&
        check_eq("short_cover_partial.avg_price", engine.position().avg_price, px(10)) &&
        check_eq("short_cover_partial.realized", engine.pnl().realized_pnl, px(2) * 50);
}

bool check_identity(const char* label, AccountingEngine& engine, const Price mark) {
    engine.mark_to_market(mark);
    const std::int64_t left = engine.pnl().realized_pnl + engine.pnl().unrealized_pnl;
    const std::int64_t right = engine.pnl().cash + engine.position().net_qty * mark;
    return check_eq(label, left, right);
}

bool test_non_integral_cost_and_partial_close() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, 10, 3, Side::Buy}) ||
        !engine.apply_fill(Fill{2, 13, 2, Side::Buy})) {
        return false;
    }
    if (!check_eq("non_integral.net_qty", engine.position().net_qty, 5) ||
        !check_eq("non_integral.avg_display", engine.position().avg_price, 11) ||
        !check_identity("non_integral.identity", engine, 12)) {
        return false;
    }

    if (!engine.apply_fill(Fill{3, 12, 2, Side::Sell}) ||
        !check_eq("non_integral_close.net_qty", engine.position().net_qty, 3) ||
        !check_eq("non_integral_close.avg_display", engine.position().avg_price, 11) ||
        !check_eq("non_integral_close.realized", engine.pnl().realized_pnl, 1) ||
        !check_identity("non_integral_close.identity", engine, 12)) {
        return false;
    }

    return engine.apply_fill(Fill{4, 12, 3, Side::Sell}) &&
        check_eq("flat.net_qty", engine.position().net_qty, 0) &&
        check_eq("flat.avg_price", engine.position().avg_price, 0) &&
        check_eq("flat.unrealized", engine.pnl().unrealized_pnl, 0) &&
        check_identity("flat.identity", engine, 15);
}

bool test_short_flip_long() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, 13, 3, Side::Sell}) ||
        !engine.apply_fill(Fill{2, 10, 5, Side::Buy})) {
        return false;
    }
    return check_eq("short_flip.net_qty", engine.position().net_qty, 2) &&
        check_eq("short_flip.avg_price", engine.position().avg_price, 10) &&
        check_eq("short_flip.realized", engine.pnl().realized_pnl, 9) &&
        check_identity("short_flip.identity", engine, 11);
}

bool test_reject_non_positive_quantity_without_mutation() {
    AccountingEngine engine;
    if (!engine.apply_fill(Fill{1, 100, 5, Side::Buy})) {
        return false;
    }
    engine.mark_to_market(110);
    const llt::PositionState position = engine.position();
    const llt::PnlState pnl = engine.pnl();

    if (engine.apply_fill(Fill{2, 200, 0, Side::Sell}) ||
        engine.apply_fill(Fill{3, 200, -4, Side::Sell})) {
        return false;
    }
    return check_eq("reject.net_qty", engine.position().net_qty, position.net_qty) &&
        check_eq("reject.avg_price", engine.position().avg_price, position.avg_price) &&
        check_eq("reject.cash", engine.pnl().cash, pnl.cash) &&
        check_eq("reject.realized", engine.pnl().realized_pnl, pnl.realized_pnl) &&
        check_eq("reject.unrealized", engine.pnl().unrealized_pnl, pnl.unrealized_pnl);
}

bool test_identity_property() {
    AccountingEngine engine;
    std::mt19937 random{0xACC7U};
    std::uniform_int_distribution<std::int32_t> quantity(1, 200);
    std::uniform_int_distribution<std::int64_t> price(1, 200000);
    std::uniform_int_distribution<int> side(0, 1);
    std::uniform_int_distribution<std::int64_t> mark_offset(-1000, 1000);

    for (std::int32_t step = 1; step <= 10000; ++step) {
        const Price fill_price = price(random);
        const Fill fill{
            step,
            fill_price,
            quantity(random),
            side(random) == 0 ? Side::Buy : Side::Sell,
        };
        if (!engine.apply_fill(fill)) {
            std::cerr << "property valid fill rejected at step=" << step << '\n';
            return false;
        }
        const Price mark = fill_price + mark_offset(random);
        if (!check_identity("property.identity", engine, mark)) {
            std::cerr << "property failed at step=" << step << '\n';
            return false;
        }

        if (step % 97 == 0) {
            const llt::PositionState before_position = engine.position();
            const llt::PnlState before_pnl = engine.pnl();
            if (engine.apply_fill(Fill{step, fill_price, 0, Side::Buy}) ||
                engine.position().net_qty != before_position.net_qty ||
                engine.position().avg_price != before_position.avg_price ||
                engine.pnl().cash != before_pnl.cash ||
                engine.pnl().realized_pnl != before_pnl.realized_pnl ||
                engine.pnl().unrealized_pnl != before_pnl.unrealized_pnl) {
                std::cerr << "property rejection mutated state at step=" << step << '\n';
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main() {
    const bool ok =
        test_basic_open() &&
        test_same_side_add() &&
        test_partial_close() &&
        test_full_close() &&
        test_flip_position() &&
        test_short_cover_partial() &&
        test_non_integral_cost_and_partial_close() &&
        test_short_flip_long() &&
        test_reject_non_positive_quantity_without_mutation() &&
        test_identity_property();

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "accounting_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
