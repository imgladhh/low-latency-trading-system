# Replay Benchmark Contract

`bench_replay` measures **in-process replay latency** for this teaching kernel. Its numbers are
not TCP, FIX, NIC, wire, exchange-round-trip, or network end-to-end latency.

## Reproducible reference run

Build in Release mode, generate the fixed-seed dataset, then run the committed configuration:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target generate_replay bench_replay
.\build\generate_replay.exe .\build\benchmark\replay_100k.csv 100000
.\build\bench_replay.exe .\build\benchmark\replay_100k.csv --warmup 2 --measured 5 --emit-csv .\build\benchmark\results.csv
```

The reference values are committed in `bench/benchmark.conf`. Binary-mode generation makes its
line endings platform-independent; the 100,000-tick reference hash is `0xa246f2daf64f6e31`.
The generator uses the fixed seed shown there and `bench_replay` prints the dataset hash, tick count, compiler, build type,
Git revision, hardware concurrency, trial counts, event checksum, drops, and collector overflows.
Keep the raw stdout and optional CSV with any published result.

## Measurement boundaries

- Input parsing and dataset generation occur before all measured trials.
- Every warm-up and measured trial constructs fresh strategy, risk, gateway, OMS, accounting,
  queue, and latency-collector state. Warm-up results are discarded.
- Module collectors surround their named in-process function calls.
- `end_to_end` begins at tick processing and ends after synchronous business work and event
  submission for that tick.
- Sync mode includes the injected in-memory event consumer call. Async mode measures enqueue on
  the replay thread; background consumer work is deliberately outside `end_to_end`.
- Summary copies and sorting occur after each replay, outside the recorded boundaries.

The harness runs both sync and async modes. A measured trial is invalid if an event is dropped,
a latency collector overflows, the sink fails, accepted/persisted event counts differ, or its
event checksum differs from the first measured trial in that mode. p99 and tail-99 require at
least 100 samples; p99.9 requires at least 1,000, matching `LatencyStats` reporting policy.

CI uses a small existing deterministic fixture as a smoke test and intentionally enforces no
nanosecond performance threshold. Publish performance numbers only from the 100,000-tick reference
configuration (or another fully recorded dataset/configuration), on identified hardware.
