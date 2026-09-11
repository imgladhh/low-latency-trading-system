# Low Latency Trading System — Correctness and Resume-Readiness Specification

Status: Draft
Last updated: 2026-09-10
Repository scope: this repository only

## 1. Purpose

This specification defines the next remediation and validation work for the deterministic C++20 trading kernel.

The project is a teaching and resume project. The goal is therefore not to reproduce a production exchange or trading stack. The goal is to make the implemented behavior internally correct, deterministic, testable, and defensible in an interview.

The implementation order in this document is mandatory unless a newly discovered correctness defect requires reprioritization.

## 2. Product Positioning

The project remains:

- single process;
- single symbol;
- replay driven;
- deterministic at the business-state level;
- fixed-point and accounting focused;
- backed by a deterministic in-process mock venue;
- intentionally smaller than a production trading platform.

## 3. Severity Definitions

- **P0 — repository or safety blocker:** must be addressed before broad staging or release work.
- **P1 — correctness blocker:** can produce inconsistent business state, silently corrupt replay input, or invalidate a central project claim.
- **P2 — evidence and robustness:** does not normally corrupt the current happy path, but weakens measurement, diagnostics, or test credibility.
- **P3 — cleanup:** improves clarity and maintainability without changing core behavior.

## 4. Explicit Non-Goals

The following work is deferred and must not block this remediation plan:

- real TCP, FIX, epoll, kernel-bypass, or NIC integration;
- production venue-session sequencing and gap recovery;
- durable execution-ID deduplication across restarts;
- snapshots, crash recovery, and journal reconstruction;
- multi-symbol sharding or portfolio-wide distributed risk;
- a complete L2/L3 order book or calibrated queue model;
- NUMA tuning, CPU isolation, locked memory, or custom allocators;
- a general-purpose or multi-type object pool in this repository;
- production-grade authentication, deployment, or operations infrastructure.

If these topics are mentioned in documentation, they must be labeled as limitations or future work rather than current capabilities.

The independent sibling `_external_hft_engine` repository has its own fixed-capacity `Order` pool. That specialized pool is retained in its own project, but it is outside the repository scope and implementation requirements of this specification.

## 5. Required Work, in Execution Order

### Phase 0 — Repository Hygiene

#### HYGIENE-001 [P0] Exclude browser profile data

Implementation status: **Complete (2026-09-10)**. The ignore rule is active, no browser-profile file is tracked, and the local directory was preserved.

Problem:

- `edge_user_data/` is currently untracked and resembles a browser profile containing caches, sessions, and local databases.
- A broad `git add .` could stage irrelevant or sensitive data.

Requirements:

- Add `edge_user_data/` to `.gitignore`.
- Verify that no file below `edge_user_data/` is tracked.
- Do not delete the local directory without explicit approval.
- Review `resume.html`, `resume.pdf`, and `resume.tex` separately and decide intentionally whether they belong in this repository.

Current resume-artifact disposition: they remain untracked and are excluded from the Phase 0 change. Whether this repository should intentionally publish them is a separate portfolio-packaging decision.

Acceptance criteria:

- `git status --short` no longer lists `edge_user_data/`.
- `git ls-files edge_user_data` returns no files.

---

### Phase 1 — Aggressive Order Lifecycle Correctness

#### EXEC-001 [P1] Define aggressive orders as IOC

Implementation status: **Complete (2026-09-10)**. Aggressive orders now produce a deterministic IOC lifecycle across the simulator, gateway, OMS, and accounting integration tests.

Current defect:

- `ExecutionSimulator::aggressive_fill()` reports `leaves_qty == 0` after a partial fill and after a no-fill result.
- `MockVenueGateway` therefore closes its active order.
- `OrderManager` independently retains the unfilled quantity and remains `PartiallyFilled` or `Acked`.
- The venue and OMS can permanently disagree about whether an order is live.

Required behavior:

- Aggressive orders use explicit immediate-or-cancel semantics.
- A full fill produces `NewAck`, then `Fill`, and ends in `Filled`.
- A partial fill produces `NewAck`, then `Fill`, then `VenueEventType::Expired`, and ends in `OrderState::Expired`.
- A zero-fill aggressive order produces `NewAck`, then `VenueEventType::Expired`, and ends in `OrderState::Expired`.
- After a terminal event, both gateway and OMS must report zero live leaves.
- `cum_qty` must equal the sum of accepted fills.

Recommended minimal model:

- Add `VenueEventType::Expired`.
- Permit `VenueEventType::Expired` while the OMS is in `OrderState::Acked` or `OrderState::PartiallyFilled`.
- Add a distinct `OrderState::Expired`; do not collapse venue-expired IOC remainder and user-requested cancel acknowledgment into `Canceled`.
- Set OMS `leaves_qty` to zero on expiration without changing `cum_qty`.
- Applying `VenueEventType::Expired` successfully uses the normal success path (`true` until OMS-002 is implemented, then `Applied`); it does not require a special outcome.
- `VenueEventType::Expired` must match the active `venue_order_id`. A mismatched event fails without mutation (`false` until OMS-002 is implemented, then `WrongOrder`).
- `VenueEventType::Expired` received before acknowledgment or after any terminal state (`Filled`, `Canceled`, `Rejected`, or `Expired`) fails without mutation (`false` until OMS-002 is implemented, then `WrongState`).
- Preserve the initiating terminal cause directly in OMS state so state-transition logs and metrics can distinguish `Expired` from `Canceled` without consulting another stream.
- Because both enums intentionally use the member name `Expired`, logs, test labels, assertion messages, and design documentation must spell them as `VenueEventType::Expired` and `OrderState::Expired`; unqualified `Expired` is not permitted in diagnostic text.

Required tests:

- aggressive full fill;
- aggressive partial fill followed by expiration;
- aggressive zero fill followed by expiration;
- ability to submit a new order after each terminal result;
- cross-module assertions that gateway and OMS agree on live/terminal status, cumulative quantity, and leaves quantity.
- wrong-order and terminal-state `VenueEventType::Expired` events are rejected without mutation.

`VenueEventBatch` is a per-`on_tick()` FIFO batch. The gateway admits at most one pending or active order, so one aggressive submission emits at most three events (`NewAck`, optional `Fill`, `Expired`) and fits the current capacity of four.

The existing test that expects zero leaves directly from a partial `ExecutionReport` must be replaced with assertions for the complete IOC lifecycle.

Acceptance criteria:

- No scenario leaves the OMS live when the gateway has no active order.
- For every order: `0 <= cum_qty <= order_qty` and terminal orders have `leaves_qty == 0`.
- All new lifecycle tests pass.

---

### Phase 2 — Replay Input Integrity

#### INPUT-001 [P1] Fail fast on malformed CSV rows

Current defect:

- `CsvReader::read_all()` silently skips rows that fail parsing.
- The replay can complete using a different event stream than the input file appears to contain.

Requirements:

- Return a structured load result or throw/catch a dedicated parse error.
- Report the input path, one-based line number, and error category.
- Do not silently skip malformed data rows.
- Distinguish an unreadable file, a header-only file, and a malformed file.

The public error category must distinguish at least:

- `IoError`: the file cannot be opened or read;
- `SchemaError`: wrong field count, empty required field, incomplete integer parse, or trailing junk;
- `ValueError`: a parsed value violates a per-row market invariant;
- `OrderingError`: timestamps regress across otherwise valid rows;
- `MultiSymbolError`: a valid row introduces a second symbol into the single-symbol replay.

#### INPUT-002 [P1] Validate market-data invariants

Schema validation must cover:

- exactly seven fields;
- complete integer parsing with no trailing junk;

Parsed-value validation must cover:

- positive prices;
- non-negative displayed quantities;
- `bid_price <= ask_price`;
- `receive_ts_ns >= exchange_ts_ns`;

Cross-row validation must cover:

- nondecreasing replay receive timestamps;
- one symbol for the current single-symbol executable.

Each failed check must map to the error categories defined by INPUT-001: schema checks use `SchemaError`, per-row market checks use `ValueError`, timestamp regression uses `OrderingError`, and symbol changes use `MultiSymbolError`.

A trailing carriage return on CRLF input must either be accepted explicitly or reported clearly.

Required tests:

- valid LF and CRLF input;
- missing, extra, empty, and nonnumeric fields;
- crossed market;
- negative quantity;
- regressing timestamp;
- multiple symbols;
- malformed middle row fails the entire load instead of being skipped.

Acceptance criteria:

- Invalid input never produces a partial successful replay.
- Every rejected fixture produces a stable, specific diagnostic.

---

### Phase 3 — OMS Event Correlation and Error Visibility

#### OMS-001 [P1] Validate venue order identity

Requirements:

- `NewAck` must provide a valid nonzero venue order ID.
- After acknowledgment, `VenueEventType::Fill`, `VenueEventType::CancelAck`, `VenueEventType::CancelReject`, and `VenueEventType::Expired` must match the active `venue_order_id`.
- Before `VenueEventType::NewAck` is applied, any `VenueEventType::Fill`, `VenueEventType::CancelAck`, `VenueEventType::CancelReject`, or `VenueEventType::Expired` event must return `WrongState` and must not mutate business state.
- A wrong-order event must not mutate order state, quantities, accounting, or journaled fills.
- `NewReject` may use venue order ID zero because no venue order was established.

#### OMS-002 [P1] Replace silent boolean failure with an explicit outcome

Replace the ambiguous `bool OrderManager::on_venue_event(...)` result with an outcome that distinguishes at least:

- `Applied`;
- `WrongState`;
- `WrongOrder`;
- `InvalidQuantity`;
- `InvalidVenueOrderId`.

Requirements:

- `main` must count and expose rejected venue events.
- Invalid events must not be silently ignored.
- Error reporting must remain deterministic.

#### OMS-003 [P2] Handle internal gateway command failure

- Do not ignore `MockVenueGateway::send_cancel()` failure after the OMS has entered `PendingCancel`.
- Either verify the gateway precondition before the OMS transition or immediately apply an explicit deterministic cancel rejection/rollback.

Required tests:

- wrong venue ID fill;
- wrong-state fill;
- zero, negative, and overfill quantities;
- cancel acknowledgment for the wrong order;
- gateway cancel submission failure does not strand the OMS in `PendingCancel`.

Deferred:

- recognizing duplicate partial fills requires execution identity and is not required while the mock gateway guarantees unique ordered events;
- session sequence numbers, replay-gap recovery, and durable deduplication remain production-only future work.

---

### Phase 4 — Deterministic Replay Contract and CI

#### DET-001 [P1] Define deterministic output precisely

The deterministic contract applies to:

- accepted business events in order;
- fills;
- OMS state transitions;
- deterministic rejects;
- final position, cash, realized PnL, and unrealized PnL.

The contract does not apply to:

- measured wall-clock latency values;
- thread scheduling;
- process-specific paths or environment metadata.

README wording must use “deterministic business state and event ordering” rather than implying that complete stdout is byte-identical.

#### DET-002 [P1] Define async journal validity

- Queue saturation in async mode may depend on consumer scheduling.
- A replay artifact is valid for deterministic comparison only when `dropped_async_events == 0`.
- Any nonzero `dropped_async_events` value must mark the run failed through the same nonzero-exit or explicit failed-run mechanism required by PERSIST-001; a warning-only successful run is not sufficient.
- Sync mode is the reference mode for golden journal generation.
- Async mode may be compared with sync mode only after removing nondeterministic latency output and confirming zero drops.

#### DET-003 [P1] Add deterministic integration fixtures

Create a small set of frozen fixtures covering:

- aggressive full fill;
- aggressive partial IOC expiration;
- passive partial fills;
- cancel acknowledgment;
- deterministic venue rejection;
- risk rejection.

CI requirements:

- configure and build in Release mode;
- run all unit and integration tests;
- run each golden replay at least twice;
- compare normalized business artifacts byte-for-byte;
- fail on nonzero async drops when testing async equivalence;
- keep latency measurements out of deterministic golden files.

Acceptance criteria:

- Repeated runs produce identical normalized business artifacts.
- A deliberate business-state change causes the replay-diff gate to fail.

---

### Phase 5 — Accounting Invariants

#### ACCT-001 [P1] Preserve exact cost basis across non-divisible averages

Current risk:

- Weighted average price is calculated using integer division.
- Discarded remainder can make reported realized plus unrealized PnL disagree with cash plus marked inventory.

Requirements:

- Store exact aggregate open-position cost basis, or preserve the division remainder explicitly.
- Treat `avg_price` as a derived/display value when exact division is impossible.
- Define one documented rounding policy at the external display boundary.
- Reject non-positive fill quantities at the accounting API boundary.
- When `net_qty == 0`, exact open cost basis and any remainder must be cleared, `avg_price` must be exactly zero, and unrealized PnL must be exactly zero.

Accounting model boundary:

- The current model has no fees, commissions, borrow costs, interest, rebates, or other carry.
- `cash` is changed only by confirmed trade notional.
- If fees or other cash flows are introduced later, every confirmed amount must flow through `cash`, and the reconciliation invariant and tests must include those cash flows explicitly.

Required invariant:

For a chosen mark price, within the documented display rounding rule:

```text
realized_pnl + unrealized_pnl
==
cash + net_qty * mark_price
```

Required tests:

- same-side additions whose weighted average is not integral;
- partial close after a non-integral average;
- full close;
- position flip;
- negative and zero fill rejection;
- a deterministic sequence/property test that checks the invariant after every event.

Acceptance criteria:

- No cost-basis remainder is silently discarded from the source of truth.
- Existing accounting scenarios continue to pass.

---

### Phase 6 — Configuration and Persistence Errors

#### CONFIG-001 [P2] Parse numeric configuration strictly

- Replace unchecked `strtoll` usage with complete `from_chars` validation.
- Reject empty input, trailing characters, overflow, and negative cancel durations.
- Print the invalid argument name and accepted range.

#### API-001 [P2] Validate order requests at boundaries

- `RiskEngine` must reject non-positive quantities.
- Gateway and OMS validation must remain independently defensive.
- Invalid requests must have a distinct reason rather than being classified as `MaxPosition`.

#### PERSIST-001 [P2] Surface journal write failure

- A failed stream write must not increment `persisted_event_count`.
- Check stream state during draining and after final flush.
- Return a nonzero process status or an explicit failed-run status when persistence fails.
- Report accepted, persisted, and dropped event counts separately.

Required tests:

- invalid duration strings and negative values;
- non-positive order quantity;
- unwritable persistence path;
- simulated stream failure where supported.

---

### Phase 7 — Bounded Latency Collection

#### PERF-001 [P2] Prevent latency-vector growth during replay

Current defect:

- Every collector reserves only `ticks.size()` samples.
- `execution_latency` can record twice per tick.
- `event_sink_latency` can record multiple times per tick.

Requirements:

- Pre-size bounded contiguous storage and write by index, or use another fixed-capacity collector.
- Never call an expanding `push_back()` in the replay loop.
- Track collector overflow independently for every latency stream.
- A benchmark result is invalid if any collector overflow occurred.
- Every collector must declare a compile-time or startup-configured `max_per_tick` value.
- Startup capacity must be derived from `tick_count * max_per_tick` with checked arithmetic.
- If the checked capacity calculation overflows `std::size_t` or exceeds the collector's supported capacity, initialization must stop before replay begins, return a nonzero process status, and print the collector name, `tick_count`, and `max_per_tick`.
- Every record beyond the configured bound must follow the collector's explicit overflow policy and increment its overflow counter.

Current `max_per_tick` configuration values:

- strategy: one sample per processed tick;
- risk: at most one sample per processed tick;
- end-to-end: one sample per processed tick;
- execution: at most two samples per processed tick;
- accounting: at most one accepted fill per tick under the current gateway;
- event sink: currently up to four emitted events per tick.

These values describe the current configuration, not permanent architectural facts. A gateway or event-flow change that can emit more records must update `max_per_tick` and its worst-case tests in the same change.

#### PERF-002 [P2] Make percentile reporting honest

- Document the percentile estimator.
- Do not present p99 or p99.9 as meaningful for tiny sample sets.
- Print a warning or suppress high percentiles below a documented minimum sample count.
- Keep summary sorting and allocation outside the measured replay loop.

Acceptance criteria:

- Collector storage addresses and capacities do not change during replay.
- Worst-case integration fixtures produce zero collector overflow.

---

### Phase 8 — SPSC Concurrency Evidence

#### SPSC-001 [P2] Add a real producer/consumer stress test

Required test characteristics:

- exactly one producer and one consumer thread;
- hundreds of thousands of monotonically increasing sequence values;
- verification of FIFO order, no duplicates, and no missing accepted entries;
- repeated wrap-around using a small queue;
- explicit saturation and recovery coverage;
- clean completion under repeated test runs;
- a bounded test deadline: the normal Release stress test should complete within 10 seconds on a modern desktop, while sanitizer jobs may use a separately documented limit of at most 30 seconds.

The test harness must fail with a diagnostic on timeout rather than wait indefinitely. Timing thresholds are test-hang guards, not throughput performance claims.

Where available, add a Linux ThreadSanitizer CI configuration. TSan is evidence for this component, not a substitute for the functional invariants above.

Do not replace SPSC with MPMC or a general-purpose thread pool for the current architecture.

---

### Phase 9 — Resume Evidence and Benchmark Harness

#### BENCH-001 [P2] Add a reproducible in-process benchmark

This phase begins only after Phases 1–8 pass.

Requirements:

- fixed replay datasets and committed run configuration;
- Release build metadata;
- warm-up followed by repeated measured trials;
- explicit measurement boundaries;
- sample counts large enough for reported percentiles;
- p50, p99, p99.9, max, and tail mean where statistically appropriate;
- sync versus async side-channel comparison;
- dropped-event and latency-collector-overflow counters;
- raw result artifacts or sufficient metadata to reproduce them.

All claims must be labeled as in-process replay latency. They must not be described as TCP, FIX, NIC, wire, or network end-to-end latency.

#### CI-001 [P2] Establish the basic quality gate

Required CI jobs:

- Release build;
- all CTest targets;
- deterministic golden replay;
- benchmark smoke test without hard-coded nanosecond thresholds;
- optional sanitizer jobs for memory/undefined behavior and SPSC concurrency.

---

### Phase 10 — Cleanup and Narrative

#### CLEAN-001 [P3] Remove dead and misleading code

- Remove the duplicate `EventKind::VenueReject` branch in `fill_logger.cpp`.
- Replace silent `VenueEventBatch` overflow with an assertion or counter.
- Rename `orders` to distinguish strategy signals, local submissions, and gateway submissions.
- Split risk rejects, venue rejects, and invalid venue events into separate counters.
- Correct the README event-flow order so OMS validation precedes accounting application.
- Remove or implement unused `StrategyDecision` fields.

#### CLEAN-002 [P3] Keep auxiliary interview exercises separate

The untracked `interview/thread_pool` exercise is not part of the trading hot path.

If retained:

- describe it as an auxiliary interview exercise;
- avoid linking it unnecessarily to `trading_kernel`;
- keep its tests separate from claims about the trading architecture;
- do not present a general thread pool as the intended concurrency model for this kernel.

## 6. Required Cross-Module Invariants

These invariants must be asserted in tests after every applicable event:

1. `0 <= cum_qty <= order_qty`.
2. A live order satisfies `leaves_qty == order_qty - cum_qty`.
3. A terminal order has `leaves_qty == 0`.
4. Gateway and OMS agree on whether an order is live.
5. Accounting is updated exactly once for every fill whose OMS-002 outcome is `Applied`, and never for any other outcome; gateway acceptance alone does not authorize an accounting update.
6. Wrong-order and wrong-state events do not mutate business state.
7. A replay with invalid input does not partially succeed.
8. A deterministic golden replay has zero unaccounted event loss.
9. Accounting satisfies the documented cash/position/PnL reconciliation rule.

## 7. Recommended Pull Request Sequence

Keep changes reviewable by using this sequence:

1. Repository hygiene only.
2. Aggressive IOC lifecycle plus cross-module tests.
3. Strict CSV parsing and input-validation tests.
4. OMS event outcomes and venue-order correlation.
5. Deterministic artifact definition and replay CI.
6. Exact accounting cost basis and invariant tests.
7. Configuration and persistence error handling.
8. Bounded latency collection and reporting rules.
9. Concurrent SPSC tests.
10. Benchmark harness, CI expansion, and documentation cleanup.

Do not combine benchmark-result publication with correctness changes in the same pull request.

## 8. Project Completion Definition

This remediation program is complete when:

- all P0 and P1 requirements are implemented and tested;
- all existing tests and new integration tests pass from a clean build;
- aggressive full, partial, and zero-fill lifecycles terminate consistently;
- malformed replay input fails with deterministic diagnostics;
- OMS rejects wrong-order events visibly and without mutation;
- normalized business replay artifacts are stable across repeated runs;
- accounting reconciliation invariants pass for non-integral cost bases;
- async and latency collectors expose all drops/overflows;
- README claims match measured and tested behavior;
- no production-only non-goal is represented as already implemented.

P2 benchmark and concurrency evidence should be completed before using specific latency or lock-free-performance claims on a resume. P3 cleanup may be scheduled around the higher-priority work, provided misleading documentation is corrected before publication.
