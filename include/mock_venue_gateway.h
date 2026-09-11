#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "execution_simulator.h"
#include "order_manager.h"
#include "types.h"

namespace llt {

struct GatewayNewOrder {
    std::int64_t client_order_id;
    Side side;
    std::int32_t quantity;
    Price limit_price;
    ExecutionStyle style;
    TimestampNs submit_ts_ns;
};

struct VenueEventBatch {
    std::array<VenueEvent, 4> events{};
    std::size_t count{0};
};

class MockVenueGateway {
public:
    MockVenueGateway(
        TimestampNs new_ack_latency_ns = 200,
        TimestampNs cancel_send_latency_ns = 200)
        : new_ack_latency_ns_(new_ack_latency_ns),
          cancel_send_latency_ns_(cancel_send_latency_ns) {}

    [[nodiscard]] bool send_new(const GatewayNewOrder& request) noexcept;
    [[nodiscard]] bool send_cancel(TimestampNs request_ts_ns) noexcept;
    [[nodiscard]] VenueEventBatch on_tick(const MarketTick& tick) noexcept;
    [[nodiscard]] const ExecutionStats& execution_stats() const noexcept { return execution_.stats(); }

private:
    struct PendingNew {
        bool active{false};
        GatewayNewOrder request{0, Side::Buy, 0, 0, ExecutionStyle::Aggressive, 0};
        TimestampNs ack_ready_ts_ns{0};
        bool should_reject{false};
    };

    struct ActiveOrder {
        bool active{false};
        std::int64_t client_order_id{0};
        std::int64_t venue_order_id{0};
        Side side{Side::Buy};
        Price limit_price{0};
        std::int32_t leaves_qty{0};
        bool cancel_sent{false};
        TimestampNs cancel_send_ts_ns{0};
        bool cancel_reject_once_done{false};
    };

    void push_event(VenueEventBatch& batch, const VenueEvent& event) noexcept;
    void apply_execution_report(
        VenueEventBatch& batch,
        const ExecutionReport& report,
        TimestampNs event_ts_ns) noexcept;
    void close_active_order_if_done() noexcept;

    TimestampNs new_ack_latency_ns_;
    TimestampNs cancel_send_latency_ns_;
    std::int64_t next_venue_order_id_{100000};
    PendingNew pending_new_{};
    ActiveOrder active_order_{};
    ExecutionSimulator execution_{};
};

[[nodiscard]] bool submit_cancel_request(
    MockVenueGateway& gateway,
    OrderManager& order_manager,
    TimestampNs request_ts_ns) noexcept;

}  // namespace llt
