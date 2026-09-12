#include "strategy.h"

namespace llt {

StrategyDecision Strategy::on_tick(const MarketTick& tick) const noexcept {
    if (tick.ask_price <= buy_below_) {
        return StrategyDecision{
            true,
            OrderRequest{Side::Buy, order_qty_, 0},
        };
    }

    if (tick.bid_price >= sell_above_) {
        return StrategyDecision{
            true,
            OrderRequest{Side::Sell, order_qty_, 0},
        };
    }

    return StrategyDecision{
        false,
        OrderRequest{Side::Buy, 0, 0},
    };
}

}  // namespace llt
