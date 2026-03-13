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
5. Replace shard idle polling with blocking wakeup.
   - Current state: shard run loop uses `std::this_thread::sleep_for()` when queue is empty.
   - Target: wake on producer signal (condition variable/eventfd/queue semaphore) to reduce idle CPU and wake latency.

## Connection architecture candidates (medium priority)

1. Move from single websocket session to partitioned multi-connection ingest.
   - Current state: one WS connection carries all markets/channels, so reconnect/drop blast radius is global.
   - Target: multiple WS sessions partitioned by market/channel with dedicated IO workers.
2. Add per-session recovery and resubscribe control.
   - Current state: reconnect strategy is effectively whole-feed.
   - Target: recover only affected partition/session, keep unaffected partitions live.
3. Align session partitions with router/shard topology.
   - Current state: single ingest stream fans into shard routing.
   - Target: map WS partitions to shard ownership boundaries to reduce cross-thread handoff pressure.

## OMS runtime candidates (medium priority)

1. Replace `OrderManager` mutex/deque intent queue with a bounded lock-free queue.
   - Current state: `submit()` pushes into a `std::deque` behind `std::mutex`.
   - Target: SPSC (or MPSC if needed) ring buffer with explicit backpressure policy.
2. Split OMS send/receive loops when outbound or inbound rates diverge.
   - Current state: one worker thread handles both `drain_outbound_intents()` and `pump_incoming_update()`.
   - Target: dedicated send and receive workers (or event loop) to reduce head-of-line blocking.
3. Remove avoidable OMS payload copies/allocations.
   - Current state: request build + update parse paths allocate `std::string` payloads per message.
   - Target: reuse buffers/allocators and add zero-copy parse path where exchange payload format allows.
4. Add in-flight order state store optimized for hot lookup/update.
   - Current state: updates are forwarded downstream but no local high-performance lifecycle table yet.
   - Target: pre-sized hash map or sharded table keyed by client/exchange order id with low-allocation transitions.
5. Add explicit queue pressure metrics and rejection behavior.
   - Current state: queue depth is observable, but no strict bounded rejection/timeout strategy tied to SLA.
   - Target: define and measure deterministic behavior under burst load (reject, block, or shed by policy).

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
