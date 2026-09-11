#pragma once

#include <cstdint>

namespace llt {

using TimestampNs = std::int64_t;
using Price = std::int64_t;

constexpr Price PRICE_SCALE = 10000;

enum class Side : std::uint8_t {
    Buy = 0,
    Sell = 1,
};

struct MarketTick {
    TimestampNs exchange_ts_ns;
    TimestampNs receive_ts_ns;
    Price bid_price;
    Price ask_price;
    std::uint32_t symbol_id;
    std::int32_t bid_qty;
    std::int32_t ask_qty;
};

struct Fill {
    TimestampNs ts_ns;
    Price price;
    std::int32_t quantity;
    Side side;
};

struct PositionState {
    std::int64_t net_qty;
    Price avg_price;
};

struct PnlState {
    std::int64_t realized_pnl;
    std::int64_t unrealized_pnl;
    std::int64_t cash;
};

struct OrderRequest {
    Side side;
    std::int32_t quantity;
    Price limit_price;
};

enum class ExecutionStyle : std::uint8_t {
    Aggressive = 0,
    Passive = 1,
};

enum class RejectReason : std::uint8_t {
    None = 0,
    MaxPosition = 1,
    NewRejectedByVenue = 2,
    CancelRejectedByVenue = 3,
    InvalidQuantity = 4,
};

struct OrderDecision {
    bool accepted;
    RejectReason reject_reason;
};

struct StrategyDecision {
    bool has_order;
    OrderRequest order;
    ExecutionStyle execution_style;
    bool request_cancel;
};

inline constexpr std::int64_t side_sign(const Side side) {
    return side == Side::Buy ? 1 : -1;
}

static_assert(sizeof(Fill) == 24, "Fill should stay compact on the hot path.");

}  // namespace llt
