#include "accounting_engine.h"

#include <cstdlib>

namespace llt {

namespace {

std::int64_t abs_qty(const std::int64_t qty) {
    return qty >= 0 ? qty : -qty;
}

}  // namespace

bool AccountingEngine::apply_fill(const Fill& fill) {
    if (fill.quantity <= 0) {
        return false;
    }

    const std::int64_t signed_fill_qty = side_sign(fill.side) * static_cast<std::int64_t>(fill.quantity);
    const std::int64_t current_qty = position_state_.net_qty;

    // Cash is updated for every fill independently of whether the trade opens, reduces, closes,
    // or flips the position. The remaining branches only decide how inventory and PnL evolve.
    pnl_state_.cash -= signed_fill_qty * fill.price;

    const std::int64_t residual_qty = current_qty + signed_fill_qty;
    if (current_qty == 0 || ((current_qty > 0) == (signed_fill_qty > 0))) {
        open_cost_ += signed_fill_qty * fill.price;
    } else if (residual_qty == 0) {
        open_cost_ = 0;
    } else if ((residual_qty > 0) == (current_qty > 0)) {
        // Partial-close policy: retain cost in proportion to the remaining position and truncate
        // integer division toward zero. The discarded fractional allocation is not lost: because
        // realized_pnl is derived as cash + open_cost, it deterministically enters realized PnL
        // on this close. Together with unrealized = net_qty * mark - open_cost, this preserves
        // realized + unrealized == cash + net_qty * mark after every update.
        open_cost_ = open_cost_ * abs_qty(residual_qty) / abs_qty(current_qty);
    } else {
        open_cost_ = residual_qty * fill.price;
    }

    position_state_.net_qty = residual_qty;
    refresh_derived();
    return true;
}

void AccountingEngine::mark_to_market(const Price mid_price) {
    mark_price_ = mid_price;
    refresh_derived();
}

void AccountingEngine::refresh_derived() noexcept {
    if (position_state_.net_qty == 0) {
        open_cost_ = 0;
        position_state_.avg_price = 0;
        pnl_state_.unrealized_pnl = 0;
    } else {
        position_state_.avg_price = abs_qty(open_cost_) / abs_qty(position_state_.net_qty);
        pnl_state_.unrealized_pnl = position_state_.net_qty * mark_price_ - open_cost_;
    }
    pnl_state_.realized_pnl = pnl_state_.cash + open_cost_;
}

}  // namespace llt
