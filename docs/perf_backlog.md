# Performance Backlog

This file tracks deliberate performance work I'm deferring, planning to run targeted perf PRs with before/after metrics.

## Hot-path candidates (high priority)

1. Remove JSON input copy in parsers.
   - Current state: `simdjson::padded_string` copies each payload before parse.
   - Target: parse from frame-pool backed, `SIMDJSON_PADDING`-safe buffers using `padded_string_view`.
2. Avoid full raw payload copy in `NormalizedEvent`.
   - Current state: `event.raw_payload = std::string{...}` on every parse.
   - Target: keep only extracted fields in hot path; gate raw payload capture behind debug/audit mode.
3. Reuse simdjson parser instance per thread.
   - Current state: parser object is constructed per message parse call.
   - Target: parser as thread-owned state in shard/parser runner.
4. Remove `FramePool` mutex from producer/consumer hot path.
   - Current state: free-list management still uses a mutex.
   - Target: SPSC-safe lock-free slot lifecycle (or split ownership model) with generation checks retained.

## Data model and routing candidates (medium priority)

1. Make raw frame to parser handoff fully zero-copy.
   - Preserve frame handle/view lifetime through parse + route stage.
2. Reduce dynamic allocations in parsed events.
   - Evaluate arena/pmr for vectors/strings used in snapshot/trade decode.
3. Evaluate control-plane split enforcement.
   - Ensure non-market messages never hit shard parse path.
4. Decide trade ID mode.
   - Keep `std::optional<std::string>` for correctness/audit.
   - Option: hash-only trade ID in strict low-latency mode.

## Potential abstraction overhead checks (medium/low priority)

1. Measure virtual/interface overhead in ingest boundaries.
   - `IWsMessageSink` and other interface calls are likely minor, but validate in profile.
2. Validate parser dispatch strategy.
   - Keep compile-time exchange parser selection where possible.
3. Cacheline tuning and false-sharing checks.
   - Re-verify `alignas` and atomic placement in SPSC queue structures.

## Benchmark plan for each perf PR

1. Capture baseline metrics (p50/p99 message latency, throughput, alloc count, CPU).
2. Land one optimization family per PR.
3. Re-run same workload and include diff in PR description.
4. Keep correctness tests and replay/regression tests green.
