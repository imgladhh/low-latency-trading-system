#pragma once

#include <cstdint>
#include <vector>

#include "execution_simulator.h"
#include "fill_logger.h"
#include "latency_stats.h"
#include "order_manager.h"
#include "types.h"

namespace llt {

enum class ReplaySinkMode : std::uint8_t {
    Sync = 0,
    Async = 1,
};

class ReplayEventSink {
public:
    virtual ~ReplayEventSink() = default;
    [[nodiscard]] virtual bool consume(const TradeEvent& event) noexcept = 0;
};

struct ReplayConfig {
    ReplaySinkMode sink_mode{ReplaySinkMode::Sync};
    ExecutionStyle execution_style{ExecutionStyle::Aggressive};
    TimestampNs passive_cancel_after_ns{3000};
};

struct ReplayCounters {
    std::int64_t orders{0};
    std::int64_t rejects{0};
    std::int64_t fills{0};
    std::int64_t accepted_events{0};
    std::int64_t persisted_events{0};
    std::int64_t dropped_events{0};
    std::int64_t rejected_venue_events{0};
    std::int64_t wrong_state_events{0};
    std::int64_t wrong_order_events{0};
    std::int64_t invalid_quantity_events{0};
    std::int64_t invalid_venue_order_id_events{0};
    std::int64_t accounting_rejected_fills{0};
};

struct ReplayLatencies {
    LatencyStats strategy;
    LatencyStats risk;
    LatencyStats execution;
    LatencyStats accounting;
    LatencyStats event_sink;
    LatencyStats end_to_end;
};

struct ReplayResult {
    ReplayCounters counters;
    PositionState position;
    PnlState pnl;
    ManagedOrder order;
    ExecutionStats execution_stats;
    ReplayLatencies latencies;
    bool sink_failed{false};
    bool latency_overflow{false};
};

[[nodiscard]] ReplayResult run_replay(
    const std::vector<MarketTick>& ticks,
    const ReplayConfig& config,
    ReplayEventSink& sink);

}  // namespace llt
