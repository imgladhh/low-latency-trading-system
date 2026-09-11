# Deterministic Replay Contract

For the same valid replay input, executable version, and run configuration, this project guarantees deterministic:

- accepted business events and their order;
- fills and OMS state transitions;
- risk, venue, and invalid-event rejection outcomes;
- final position, cash, realized PnL, and unrealized PnL.

The contract does not cover measured wall-clock latency, OS thread scheduling, process-specific paths, or environment metadata. Complete stdout is therefore not expected to be byte-identical.

Sync mode is the reference for committed golden artifacts. A normalized artifact contains the persisted business-event journal followed by selected final business counters and state; latency summaries and paths are excluded.

Async mode is valid for comparison only when `dropped_async_events=0`. Any nonzero drop count sets `run_status=failed` and causes a nonzero process exit. A zero-drop async journal must match the sync journal for the same fixture.
