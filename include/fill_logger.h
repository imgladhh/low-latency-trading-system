#pragma once

#include <cstdint>
#include <iosfwd>

#include "types.h"

namespace llt {

enum class EventKind : std::uint8_t {
    Fill = 0,
    RiskReject = 1,
    OmsTransition = 2,
    VenueReject = 3,
};

// TradeEvent is the side-channel payload that crosses the hot-path boundary. It stays small and
// POD-like so enqueue/dequeue work is cheap compared with formatting and I/O.
struct TradeEvent {
    TimestampNs ts_ns;
    Price price;
    std::int32_t quantity;
    EventKind kind;
    Side side;
    RejectReason reject_reason;
};

[[nodiscard]] const char* reject_reason_name(RejectReason reason) noexcept;
void write_trade_event_log(std::ostream& out, const TradeEvent& event);
[[nodiscard]] bool write_trade_event_persistence(std::ostream& out, const TradeEvent& event);

static_assert(sizeof(TradeEvent) == 24, "TradeEvent should stay compact for queue traffic.");

}  // namespace llt
