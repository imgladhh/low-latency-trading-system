# Low Latency Trading System

A replay-driven C++20 trading kernel for learning low-latency architecture with correctness first. Given valid input and a fixed run configuration, business state and accepted event ordering are deterministic.

Detailed implementation history (all completed phases and module deep-dives):
- [Implemented Phase Details](./docs/implemented_history.md)

## What This Repo Is

- single process, single symbol core
- deterministic business state and event ordering for the same valid input and run configuration
- fixed-point price + accounting-first state transitions
- teaching-oriented architecture that can evolve toward production patterns

## Current Big Architecture

Hot path (synchronous):

`MarketTick -> Strategy -> Risk -> Execution -> Accounting -> OMS state update`

Cold/side path (optional async):

`event sink/logging/persistence` via SPSC ring buffer (`sync` vs `async` modes for A/B latency comparison)

Measured latency, thread scheduling, output paths, and other environment metadata are not byte-stable replay outputs. Sync persistence is the golden reference. Async output is comparable only when `dropped_async_events=0`; any drop fails the run. See [Deterministic Replay Contract](./docs/determinism.md).

## Principles (Non-Negotiable)

- determinism over convenience
- fixed-point integer pricing (no float PnL logic)
- accounting correctness before performance
- explicit state machines (execution + order lifecycle)
- measure tail latency, not just averages

## Roadmap (Execution Order)

Completed:

- Phase 1: minimal runnable deterministic kernel
- Phase 2: latency instrumentation (`p50/p99/p99.9/max/tail mean`)
- Phase 3: SPSC async side-channel decoupling
- Phase 4: memory/layout cleanup and hot-path struct discipline
- Phase 5: more realistic execution simulation (partial fill/queue/cancel latency signals)
- Phase 6: OMS + mock gateway + order lifecycle state transitions
- Phase 6.5: OMS transition observability and reject-path visibility

Planned (priority order):

- Phase 7-A: CI deterministic replay gate (highest priority)
  - add replay-diff check in CI and fail build when output drifts unexpectedly
  - lock deterministic run config (input file, mode, seed policy if needed)
  - done when: every PR must pass deterministic replay regression checks

- Phase 7-B: benchmark harness + frozen datasets
  - add repeatable benchmark scripts with fixed replay datasets
  - report `p50/p99/p99.9/max/tail_mean` for baseline scenarios
  - include sync vs async side-channel comparison
  - done when: latency tables are reproducible across runs and commits

- Phase 7-C: risk realism upgrade
  - add `max_loss`, `kill_switch`, and stale-market-data guard
  - expose explicit reject reasons and risk counters in run summary
  - done when: risk controls are test-covered and visible in replay outputs

- Phase 7-D: failure-oriented behavior tests
  - model and test late fill vs cancel ack race
  - model and test duplicate/out-of-order venue events
  - add replay-gap / malformed-tick handling policy with tests
  - done when: failure-path semantics are deterministic and unit/integration tested

- Phase 8: property-style invariants + recovery semantics
  - add randomized invariants for accounting/risk/order-state correctness
  - add snapshot + journal replay recovery check (state reconstruction equality)
  - done when: post-recovery state is verified equal to pre-crash reference state

## Immediate TODOs

- implement Phase 7-A CI deterministic replay gate
- implement Phase 7-B benchmark harness and publish first latency evidence table
- implement Phase 7-C risk controls (`max_loss`, `kill_switch`, stale-data guard)
- implement Phase 7-D failure-path deterministic tests
- prepare Phase 8 invariants + recovery scaffolding

## Build & Run

Build:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run:

```powershell
.\build\trading_main.exe .\data\sample_replay.csv async .\data\event_journal_async.csv
```

General CLI:

```text
trading_main <csv-path> [sync|async] [persistence-path] [aggressive|passive] [passive-cancel-after-ns]
```
