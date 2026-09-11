#include "fill_logger.h"

#include <ostream>

#include "order_manager.h"

namespace llt {

const char* reject_reason_name(const RejectReason reason) noexcept {
    switch (reason) {
    case RejectReason::None:
        return "NONE";
    case RejectReason::MaxPosition:
        return "MAX_POSITION";
    case RejectReason::NewRejectedByVenue:
        return "NEW_REJECTED_BY_VENUE";
    case RejectReason::CancelRejectedByVenue:
        return "CANCEL_REJECTED_BY_VENUE";
    case RejectReason::InvalidQuantity:
        return "INVALID_QUANTITY";
    }

    return "UNKNOWN";
}

void write_trade_event_log(std::ostream& out, const TradeEvent& event) {
    // Formatting belongs on the sink side of the boundary, not in the trading logic, because
    // string/stream output is exactly the sort of work that adds jitter to the hot path.
    if (event.kind == EventKind::Fill) {
        out
            << "fill ts_ns=" << event.ts_ns
            << " side=" << (event.side == Side::Buy ? "BUY" : "SELL")
            << " price=" << event.price
            << " qty=" << event.quantity
            << '\n';
        return;
    }

    if (event.kind == EventKind::OmsTransition) {
        out
            << "oms_transition ts_ns=" << event.ts_ns
            << " from=" << order_state_name(static_cast<OrderState>(event.price))
            << " to=" << order_state_name(static_cast<OrderState>(event.quantity))
            << '\n';
        return;
    }

    if (event.kind == EventKind::VenueReject) {
        out
            << "venue_reject ts_ns=" << event.ts_ns
            << " side=" << (event.side == Side::Buy ? "BUY" : "SELL")
            << " qty=" << event.quantity
            << " reason=" << reject_reason_name(event.reject_reason)
            << '\n';
        return;
    }

    if (event.kind == EventKind::VenueReject) {
        out
            << "venue_reject ts_ns=" << event.ts_ns
            << " side=" << (event.side == Side::Buy ? "BUY" : "SELL")
            << " qty=" << event.quantity
            << " reason=" << reject_reason_name(event.reject_reason)
            << '\n';
        return;
    }

    out
        << "reject ts_ns=" << event.ts_ns
        << " side=" << (event.side == Side::Buy ? "BUY" : "SELL")
        << " qty=" << event.quantity
        << " reason=" << reject_reason_name(event.reject_reason)
        << '\n';
}

bool write_trade_event_persistence(std::ostream& out, const TradeEvent& event) {
    // Persistence uses a simple CSV journal so Phase 3 can measure the cost of side-channel
    // durability separately from the trading-state transitions themselves.
    out
        << (event.kind == EventKind::Fill
                ? "FILL"
                : (event.kind == EventKind::RiskReject
                    ? "RISK_REJECT"
                    : (event.kind == EventKind::OmsTransition ? "OMS_TRANSITION" : "VENUE_REJECT")))
        << ','
        << event.ts_ns
        << ','
        << (event.side == Side::Buy ? "BUY" : "SELL")
        << ','
        << event.price
        << ','
        << event.quantity
        << ','
        << reject_reason_name(event.reject_reason)
        << '\n';
    return static_cast<bool>(out);
}

}  // namespace llt
