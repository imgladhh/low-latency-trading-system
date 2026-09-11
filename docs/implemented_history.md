# Low Latency Trading System

## Overview

This project is a teaching-oriented low-latency trading kernel implemented in C++.

The goal is not to build a feature-complete trading platform. The goal is to build a small, deterministic, correctness-first kernel that is easy to reason about, easy to validate, and easy to evolve into a more realistic low-latency system.

The design priority is:

1. Correctness
2. Observability
3. Determinism
4. Extensibility toward low-latency architecture

## MVP Goal

Build a:

- single-process
- single-threaded
- single-symbol
- replayable
- deterministic
- correctness-verifiable

trading kernel.

The most important requirement is that the same CSV input must produce the same output on every run.

## Core Design Principles

### 1. Determinism

Determinism is the most important requirement.

- Same input CSV must produce identical output across runs
- No wall clock may affect business logic
- Business logic should behave as pure state transition:
  - state + input -> output + new state
- Phase 1 uses only replay timestamps and input timestamps

### 2. Fixed-Point Pricing

Floating-point is forbidden in pricing and PnL logic.

```cpp
using Price = int64_t;
constexpr Price PRICE_SCALE = 10000;
```

Example:

- `123.45 -> 1234500`

PnL:

```cpp
PnL = (exit_price - entry_price) * quantity
```

Why:

- no floating-point drift
- deterministic
- fast integer arithmetic

### 3. Hot Path Memory Rules

In the hot path, do not use:

- `std::string`
- `new / delete`

Prefer:

- POD structs
- `std::vector` with `reserve`
- contiguous, cache-friendly memory layout

### 4. Timestamp Rules

```cpp
using TimestampNs = int64_t;
```

Every `MarketTick` must contain:

- `exchange_ts_ns`
- `receive_ts_ns`

Phase 1 must not use real system time in business logic.

### 5. Accounting Has Highest Priority

If accounting is wrong, the system is unusable.

If strategy is wrong, the strategy may lose money, but the kernel is still structurally sound.

Accounting correctness comes before performance work.

## Core Data Structures

The kernel is built around the following foundational structs:

```cpp
struct MarketTick {
    TimestampNs exchange_ts_ns;
    TimestampNs receive_ts_ns;
    uint32_t symbol_id;
    Price bid_price;
    Price ask_price;
    int32_t bid_qty;
    int32_t ask_qty;
};

struct Fill {
    TimestampNs ts_ns;
    Side side;
    Price price;
    int32_t quantity;
};

struct PositionState {
    int64_t net_qty;
    Price avg_price;
};

struct PnlState {
    int64_t realized_pnl;
    int64_t unrealized_pnl;
    int64_t cash;
};
```

## Phase 1 Scope

Phase 1 is a minimal runnable system.

Included:

- single-threaded event loop
- CSV market replay
- simple strategy
- simple risk engine
- simplified execution simulator
- accounting engine
- final PnL / fills / position output

What Phase 1 is really for:

- prove the event flow is correct
- prove the state machine is correct
- make accounting fully testable
- create a stable foundation for later latency work

Phase 1 explicitly does not include:

- multithreading
- network I/O
- order book
- partial fill
- cancel flow
- persistence
- multi-strategy support
- multi-symbol trading

## Phase 1 Architecture

The final kernel loop is:

```cpp
for each MarketTick:
    strategy -> Signal / OrderRequest
    risk -> accept / reject
    if accepted:
        execution simulator
        if fill:
            accounting update
```

Current module split:

- `include/types.h`
- `include/accounting_engine.h`
- `include/csv_reader.h`
- `include/execution_simulator.h`
- `include/risk_engine.h`
- `include/strategy.h`
- `src/accounting_engine.cpp`
- `src/csv_reader.cpp`
- `src/execution_simulator.cpp`
- `src/risk_engine.cpp`
- `src/strategy.cpp`
- `src/main.cpp`

## Accounting Rules

Accounting is the core of the system.

Rules:

- only adding to a position updates `avg_price`
- reducing a position does not change `avg_price`
- fully closing a position resets `avg_price` to `0`
- flipping a position must:
  - first close the old side and realize PnL
  - then open the residual quantity as a new position at fill price

Standard realized PnL formula:

```cpp
direction = (net_qty > 0 ? 1 : -1)
realized += (fill_price - avg_price) * qty * direction
```

Required accounting scenarios:

1. Open / add
2. Partial reduce
3. Full close
4. Position flip
5. Short cover behavior

### Required Accounting Tests

These scenarios must always pass:

| Scenario | Operation | Expected |
| --- | --- | --- |
| Basic open | Buy 100 @ 10.00 | `net=100 avg=100000 realized=0` |
| Same-side add | + Buy 100 @ 12.00 | `net=200 avg=110000` |
| Partial close | Sell 50 @ 15.00 | `net=50 realized=2500000` |
| Full close | Sell 100 @ 8.00 | `net=0 realized=-2000000` |
| Flip | Sell 150 @ 12.00 | `net=-50 avg=120000 realized=2000000` |
| Short cover | Buy 50 @ 8.00 | `net=-50 realized=1000000` |

## CSV Reader Specification

Fixed schema:

```text
exchange_ts_ns,receive_ts_ns,symbol_id,bid_price,ask_price,bid_qty,ask_qty
```

Rules:

- prices are already scaled integers
- do not parse floating-point
- use `std::string_view`
- use `std::from_chars`

Why:

- no allocation in parsing logic
- locale-independent
- fast and deterministic

## Roadmap

This is the agreed development roadmap and should be the default implementation guide going forward.

### Phase 1: Minimal Runnable Kernel

Goal:

- single thread
- CSV replay
- simple strategy
- simple risk
- simple execution simulator
- output fills / position / pnl

Success criteria:

- replay runs end-to-end
- accounting is correct
- tests pass
- same input produces identical output on repeated runs

### Phase 2: Latency Instrumentation

Goal:

- add timing points for each stage
- output `p50 / p99 / max`
- identify the slow stages

Timing points:

- `T0`: tick ingest
- `T1`: strategy
- `T2`: risk
- `T3`: execution / post-trade update

Focus:

- tail latency matters more than average latency
- instrumentation should not change business correctness
- keep this phase observational, not architectural

Current implementation:

- stage-level latency stats are collected for `strategy`, `risk`, `execution`, `accounting`, and `end_to_end`
- summaries currently print `count / min / mean / p50 / p99 / p99.9 / tail_mean_99 / max`
- timing uses a monotonic local clock for measurement only and does not affect business logic

### Phase 3: SPSC Ring Buffer

Goal:

- make logging and persistence asynchronous
- keep the main path non-blocking
- compare tail latency before and after

Scope:

- only offload non-critical-path work
- keep the trading path logically single-threaded
- use SPSC, not MPMC

Current implementation:

- fill logging is offloaded through a fixed-capacity SPSC ring buffer
- the binary supports both `sync` and `async` sink modes for before/after tail-latency comparison
- the main thread can enqueue `TradeEvent` records instead of writing directly to `stdout`
- a background consumer thread drains the queue and writes events in FIFO order
- both console logging and file persistence are handled through the side-channel event sink
- `fill` and `risk reject` events are supported
- `dropped_async_events` is tracked to make queue saturation visible
- event sink latency is measured as `event_enqueue_async` or `event_sink_sync`

### Phase 4: Memory and Data Layout Optimization

Goal:

- reduce dynamic allocation
- use `reserve`
- add pooling where useful
- move toward array- and id-based layouts
- compare cache behavior and latency impact

Focus:

- optimize measured bottlenecks
- prefer simple, maintainable memory improvements first

Current implementation:

- `Fill` and `TradeEvent` have been reordered to stay compact at 24 bytes
- hot-path layout assumptions are guarded with size assertions and `layout_tests`
- CSV replay now pre-counts data lines and reserves exact vector capacity before loading ticks
- persisted event counts are tracked directly instead of rereading the persistence file after replay
- the binary prints a layout summary for key hot-path structs after each run

Current limitation:

- cache hit/miss counters are not yet measured directly because this project is not wired to a hardware-performance toolchain
- Phase 4 currently improves memory/layout behavior indirectly rather than reporting true hardware cache metrics

### Phase 5: More Realistic Execution Simulator

Goal:

- partial fill
- queue position
- cancel latency
- stale quote
- adverse selection

Focus:

- make execution realism good enough to teach market microstructure tradeoffs
- avoid trying to reproduce a full exchange matching engine unless explicitly needed

Current implementation:

- aggressive orders can now partially fill instead of only all-or-nothing
- the execution simulator supports a single resting passive order with queue-ahead tracking
- cancel requests are modeled with cancel latency before an order is acknowledged canceled
- passive fills are tagged for stale-quote and adverse-selection conditions using simple deterministic rules
- execution stats are printed after each run so execution quality can be inspected separately from PnL
- runtime mode supports `aggressive` or `passive` execution behavior in the main binary
- `data/phase5_passive_replay.csv` exercises passive queueing plus cancel-latency paths end-to-end

Current limitation:

- the simulator still models only one working passive order at a time
- queue dynamics are simplified from top-of-book data rather than driven by a full matching engine
- the default strategy still emits marketable signals, so richer passive behavior relies on dedicated replay scenarios

### Phase 6: Live Connectivity And Order State Machine

Goal:

- introduce a realistic order lifecycle
- separate local order intent from external venue state
- make the kernel look more like a real trading system skeleton rather than only a replay simulator

Core scope:

- client order id and venue order id tracking
- `New -> Acked -> PartiallyFilled -> Filled / Canceled / Rejected` state machine
- cancel-in-flight handling
- outstanding leaves quantity tracking
- mock gateway / venue message flow instead of local fill-only shortcuts

Focus:

- model the difficult part of real trading infrastructure: keeping local state consistent with external state
- preserve deterministic replay and testability by driving the order manager from recorded or mocked venue events
- keep this phase single-process and testable before considering real network protocol integration

Why this phase matters:

- supporting more products makes the system wider
- order state management and connectivity make the system deeper and more production-like
- this is the most direct next step toward realistic hedge fund trading infrastructure after Phase 5

Current bootstrap implementation:

- `OrderManager` module introduced with deterministic lifecycle transitions
- venue events currently modeled: `NewAck`, `NewReject`, `Fill`, `CancelAck`, `CancelReject`
- tracks `cum_qty` and `leaves_qty` as fills arrive
- dedicated `order_manager_tests` provide initial Phase 6 correctness coverage
- main loop now routes fills/cancel acknowledgments through `OrderManager` using mocked venue events
- `MockVenueGateway` now owns venue-side timing and event emission (`NewAck/Fill/CancelAck`)
- strategy intent is converted into gateway requests, then replayed back as venue events into OMS
- `NewReject` and `CancelReject` paths are now modeled and can be observed in main-loop side-channel output
- OMS transitions are emitted as side-channel events (`OMS_TRANSITION`) for replay-time auditing

Current limitation:

- single working order flow only (no multi-order book of working orders yet)
- still no real network protocol integration; gateway remains deterministic in-process simulation
- reject behavior currently uses deterministic test-oriented rules rather than market microstructure-calibrated probabilities

### Phase 7: Resume-Ready Realism And Benchmarking

Goal:

- move from teaching kernel to interview-competitive side project
- add realistic market microstructure components and measurable engineering evidence
- make performance and correctness claims reproducible

Scope:

- add a minimal L2 top-N book view and queue-position-aware execution evolution
- extend risk controls with `max_drawdown`, `kill_switch`, and stale-data guards
- add benchmark harness with stable replay datasets and pinned run configuration
- add CI gates for build, tests, and deterministic replay regression
- publish reproducible before/after latency and tail-latency comparisons

Success criteria:

- repository includes repeatable benchmark commands and fixed datasets
- README reports `p50/p99/p99.9/max/tail_mean_99` for at least one baseline and one optimized run
- deterministic replay regression is automatically checked in CI
- risk and accounting invariants are covered by both example tests and randomized/property-style tests

Current limitation:

- not yet implemented
- this phase is the recommended next step for converting current progress into resume-ready project evidence

## Required Development Order

Implementation must follow this order:

1. `types.h`
2. `AccountingEngine`
3. the 6 accounting unit tests
4. `CsvReader`
5. `ExecutionSimulator`
6. `RiskEngine`
7. `Strategy`
8. `main` replay loop

Reason:

- accounting correctness is the hard correctness boundary
- once accounting is trusted, the rest of the event loop can be built around it

## MVP Completion Standard

The MVP is complete when all of the following are true:

- CSV can be replayed
- accounting is correct
- unit tests pass
- final pnl and final position are printed
- repeated runs on the same input produce identical output

## Interview Relevance

For hedge fund interview preparation, Phase 1 is a strong foundation, but it is best understood as the base layer rather than the full story.

Phase 1 is enough to discuss:

- determinism
- fixed-point pricing
- event-driven architecture
- accounting correctness
- state-machine thinking

To make the project stronger for more systems-oriented low-latency interviews, later phases should also be understood:

- Phase 2 for latency measurement and tail behavior
- Phase 3 for asynchronous decoupling with SPSC
- Phase 5 for execution realism and market microstructure discussion
- Phase 6 for realistic OMS/connectivity and external state consistency
- Phase 7 for benchmark evidence, production-style risk controls, and reproducible engineering claims

## Resume-Ready Upgrade Checklist

If your target is "strong side project on resume", prioritize the following in order:

1. realism upgrades (execution + queue dynamics + cost model)
2. engineering hardening (CI, deterministic regression, config discipline)
3. measurable evidence (latency tables, tail behavior, profiling deltas)

Concrete additions to land:

- market realism:
  - add L2-aware queue progression signals (still deterministic under replay)
  - include fees/slippage/impact in post-trade reporting
- risk realism:
  - add hard guards: `max_position`, `max_loss`, `kill_switch`, `stale_quote_guard`
  - emit explicit reject reasons and risk counters in summary output
- reproducibility:
  - add benchmark scripts with fixed run args and dataset references
  - add CI workflow for build + tests + deterministic replay diff check
- evidence:
  - report baseline vs optimized latency (`p50/p99/p99.9/max/tail_mean_99`)
  - report drops/backpressure counters for async side-channel saturation

Resume-style project statement template:

- "Built a deterministic C++20 low-latency trading kernel (fixed-point pricing, replay-driven OMS/execution state machines) with validated accounting invariants, async side-channel logging via SPSC ring buffer, and benchmarked tail-latency improvements (`p99/p99.9/tail mean`) under controlled replay workloads."

## Recommended Reading Order

If you want to understand the current codebase efficiently, read it in event-flow order rather than alphabetically.

### 1. Start With The Data Model

Read:

- `include/types.h`

Goal:

- understand the core state and message shapes
- see which values are signed, fixed-point, or ID-based
- build a mental model of what flows through the kernel
- notice which structs now carry Phase 4 layout intent through field ordering and size checks

### 2. Read The Main Event Loop

Read:

- `src/main.cpp`

Goal:

- understand the full replay pipeline
- identify what is hot-path logic versus cold-path reporting
- see where Phase 2 latency timing and Phase 3 side-channel boundaries are applied

What to look for:

- why `strategy`, `risk`, `execution`, and `accounting` remain synchronous
- why logging and persistence can be moved behind a queue
- how `sync` and `async` sink modes provide a controlled A/B comparison

### 3. Read The Accounting Engine Carefully

Read:

- `include/accounting_engine.h`
- `src/accounting_engine.cpp`
- `tests/accounting_tests.cpp`

Goal:

- understand the most important correctness boundary in the system
- follow how open/add, partial reduce, full close, and flip are handled
- connect each branch in `apply_fill()` to a test case

What to look for:

- why cash updates happen for every fill
- why only same-direction fills update `avg_price`
- why opposite-direction fills first realize PnL before opening any residual inventory

### 4. Read The Strategy, Risk, And Execution Modules

Read:

- `src/strategy.cpp`
- `src/risk_engine.cpp`
- `src/execution_simulator.cpp`

Goal:

- understand the synchronous decision chain inside the hot path
- see which parts are intentionally simple in Phase 1 and why

What to look for:

- strategy as signal generation
- risk as a gate on projected inventory
- execution as the source of fills that feed accounting

### 5. Read The Latency Instrumentation

Read:

- `include/latency_stats.h`
- `src/latency_stats.cpp`
- `tests/latency_stats_tests.cpp`

Goal:

- understand how the project measures `p50`, `p99`, `p99.9`, and `tail_mean_99`
- see why sample collection stays lightweight on the hot path and summary work happens later

What to look for:

- why `tail_mean_99` is useful in addition to percentile cutoffs
- why latency measurement is observational and does not affect business logic

### 6. Read The Phase 3 Boundary Components

Read:

- `include/spsc_ring_buffer.h`
- `include/fill_logger.h`
- `src/fill_logger.cpp`
- `tests/spsc_ring_buffer_tests.cpp`
- `tests/fill_logger_tests.cpp`

Goal:

- understand how the project separates hot-path state transitions from slow side-channel work
- see why SPSC is sufficient for the current design

What to look for:

- why the ring buffer keeps one slot empty
- why producer and consumer cursors are separated
- why formatting and persistence happen after the hot-path boundary

### 7. Read The Phase 4 Layout And Reservation Work

Read:

- `include/fill_logger.h`
- `include/layout_utils.h`
- `src/layout_utils.cpp`
- `src/csv_reader.cpp`
- `tests/layout_tests.cpp`

Goal:

- understand how the project starts treating data layout as part of low-latency design
- see where replay capacity is reserved up front
- see how hot-path structs are kept compact and explicitly checked

What to look for:

- why `Fill` and `TradeEvent` field order matters
- why exact replay reservation reduces avoidable allocations during CSV load
- why layout summaries and size assertions make memory decisions visible and testable

### 8. Read The Phase 5 Execution State Machine

Read:

- `include/execution_simulator.h`
- `src/execution_simulator.cpp`
- `tests/execution_simulator_tests.cpp`

Goal:

- understand how the simulator moved beyond immediate all-or-nothing fills
- see where partial fill, queue position, cancel latency, stale quote, and adverse selection are modeled

What to look for:

- how aggressive and passive orders now diverge
- how `leaves_qty` evolves across ticks
- why passive fill realism can be improved without yet implementing a full matching engine

### 9. Read The Phase 6 Order State Machine Bootstrap

Read:

- `include/order_manager.h`
- `src/order_manager.cpp`
- `tests/order_manager_tests.cpp`
- `include/mock_venue_gateway.h`
- `src/mock_venue_gateway.cpp`
- `tests/mock_venue_gateway_tests.cpp`

Goal:

- understand how local order state is separated from venue-confirmed state
- see deterministic transitions for ack/fill/cancel outcomes

What to look for:

- how `PendingNewAck -> Acked -> PartiallyFilled/Filled` is represented
- how cancel transitions are modeled with `PendingCancel`
- how mocked venue timing is separated from OMS state transitions

### 10. Use README Roadmap To Reconnect The Pieces

After reading the files above, return to the roadmap sections in this README.

Goal:

- connect implementation details back to the project phases
- understand which choices were made for correctness, which for observability, and which for low-latency structure
- understand why the next major realism step is order-state management and venue interaction rather than just broader product coverage

## Current Status

Implemented:

- project skeleton
- core types
- accounting engine
- accounting unit tests
- latency stats unit tests
- SPSC ring buffer unit tests
- event formatting unit tests
- layout unit tests
- execution simulator unit tests
- order manager unit tests
- mock venue gateway unit tests
- CSV replay reader
- simple risk engine
- simple threshold strategy
- single-threaded main replay loop
- latency stats collector
- stage-level latency instrumentation
- asynchronous fill logging via SPSC ring buffer
- configurable sync vs async side-channel event sink
- asynchronous event persistence
- compacted hot-path event layout and exact replay reservation
- more realistic execution simulator with partial fills and passive order state
- phase-6 order lifecycle bootstrap module
- phase-6 mock venue gateway wiring in main loop
- phase-6.5 OMS transition and venue reject observability
- sample replay data

Verified:

- required accounting tests pass
- sample replay runs end-to-end
- repeated runs on the same input produce identical output

## Running Sync vs Async Phase 3

Usage:

```text
trading_main <csv-path> [sync|async] [persistence-path] [aggressive|passive] [passive-cancel-after-ns]
```

Examples:

```powershell
& .\build\trading_main.exe .\data\sample_replay.csv async .\build\event_journal_async.csv
& .\build\trading_main.exe .\data\sample_replay.csv sync .\build\event_journal_sync.csv
& .\build\trading_main.exe .\data\phase5_passive_replay.csv async .\build\event_journal_phase5_passive.csv passive 2500
```

The output can be compared using:

- `latency[event_enqueue_async]`
- `latency[event_sink_sync]`
- `latency[end_to_end]`

Persistence file schema:

```text
event_kind,ts_ns,side,price,quantity,reject_reason
```
