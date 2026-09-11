#pragma once

#include <cstdint>

#include "types.h"

namespace llt {

enum class OrderState : std::uint8_t {
    Idle = 0,
    PendingNewAck = 1,
    Acked = 2,
    PartiallyFilled = 3,
    PendingCancel = 4,
    Filled = 5,
    Canceled = 6,
    Rejected = 7,
    Expired = 8,
};

enum class VenueEventType : std::uint8_t {
    NewAck = 0,
    NewReject = 1,
    Fill = 2,
    CancelAck = 3,
    CancelReject = 4,
    Expired = 5,
};

enum class VenueEventOutcome : std::uint8_t {
    Applied = 0,
    WrongState = 1,
    WrongOrder = 2,
    InvalidQuantity = 3,
    InvalidVenueOrderId = 4,
};

struct VenueEvent {
    VenueEventType type;
    TimestampNs ts_ns;
    std::int64_t venue_order_id;
    std::int32_t fill_qty;
    Price fill_price;
};

struct ManagedOrder {
    std::int64_t client_order_id;
    std::int64_t venue_order_id;
    Side side;
    Price limit_price;
    std::int32_t order_qty;
    std::int32_t cum_qty;
    std::int32_t leaves_qty;
    OrderState state;
};

class OrderManager {
public:
    [[nodiscard]] bool submit_new(
        std::int64_t client_order_id,
        Side side,
        Price limit_price,
        std::int32_t qty) noexcept;

    [[nodiscard]] bool request_cancel() noexcept;
    [[nodiscard]] VenueEventOutcome on_venue_event(
        const VenueEvent& event,
        Fill& fill_out) noexcept;

    [[nodiscard]] const ManagedOrder& order() const noexcept { return order_; }

private:
    ManagedOrder order_{0, 0, Side::Buy, 0, 0, 0, 0, OrderState::Idle};
};

[[nodiscard]] const char* order_state_name(OrderState state) noexcept;
[[nodiscard]] const char* venue_event_outcome_name(VenueEventOutcome outcome) noexcept;

}  // namespace llt
