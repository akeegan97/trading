# Trading Infrastructure Engine (C++)

This repository is a systems-engineering project for building market-data and order-management plumbing used by automated trading systems.

The focus is infrastructure quality:
- deterministic ingest -> decode -> route -> shard execution flow
- explicit data contracts between stages
- testable exchange adapters (Kalshi + Polymarket decode paths)
- runtime safety surfaces (drop counters, parse failures, startup error paths)
- CI-enforced build, formatting, linting, and tests

## What Exists Today

- `trader_app`: live websocket ingest (Kalshi), message pipeline, shard-local order book state.
- `logger_app`: minimal runtime bootstrap binary.
- Pipeline stack:
  - frame pool + SPSC ingest queue
  - extract/normalize dispatch
  - keyed router and shard fanout
  - shard parser + `BookStore` apply
- OMS stack (scaffolded and tested):
  - canonical order intent/update contract
  - exchange OMS adapter interface + Kalshi implementation
  - runtime `OrderManager` with outbound/inbound loops
- Tests: parser, routing, pipeline, config loading, auth signing, OMS adapter/runtime.

## Architecture

```text
Kalshi WS -> FramePoolMessageSink -> SPSC Frame Queue -> Router -> Shard Queues
                                                            |
                                                            v
                                                  Shard Parser -> NormalizedEvent -> BookStore

Strategy -> OrderIntent -> OrderManager -> ExchangeOmsAdapter -> OrderTransport
                                                     ^                |
                                                     |                v
                                              IOrderEventSink <- parse_update
```

Pipeline contract details: [`docs/data_contract.md`](docs/data_contract.md)  
Performance roadmap: [`docs/perf_backlog.md`](docs/perf_backlog.md)

## Tech Stack

- Language: C++20
- Build: CMake + Ninja
- Dependencies: `boost`, `openssl`, `nlohmann_json`, `simdjson`, `spdlog`, `gtest` (via `vcpkg`)
- Quality gates: `clang-format`, `clang-tidy`, `ctest`, GitHub Actions CI

## Quickstart

### 1) Prerequisites

- `clang`, `clang++`, `clang-tidy`
- `cmake` (>= 3.24), `ninja`
- `git`
- `vcpkg` (recommended; CI uses manifest mode)

### 2) Configure + Build

```bash
# if using vcpkg presets
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset dev-vcpkg
cmake --build --preset build-dev-vcpkg --parallel
```

If you have all dependencies installed system-wide, you can use non-vcpkg presets:

```bash
cmake --preset dev
cmake --build --preset build-dev --parallel
```

### 3) Run Tests

```bash
ctest --preset test-dev-vcpkg
# or: ctest --preset test-dev
```

### 4) Run Trader App

```bash
# credentials required (or set inline in config)
export KALSHI_KEY_ID=...
export KALSHI_PRIVATE_KEY_PEM='-----BEGIN PRIVATE KEY-----...'

./build/dev/cpp/trader_app --config docs/trader_config.example.json
```

You can also pass config path positionally or via `TRADING_CONFIG_PATH`.

## Runtime Config

Example drop-file: [`docs/trader_config.example.json`](docs/trader_config.example.json)

Key knobs:
- mode (`paper`, `dev`, etc.)
- websocket endpoint and channel list
- frame pool/queue capacities
- shard count and queue sizing
- pump batch size and idle sleep

## Repository Layout

- `cpp/apps` executable entrypoints
- `cpp/include/trading` public interfaces and contracts
- `cpp/src` implementations
- `cpp/tests` integration and unit tests
- `docs` data contracts, config example, perf backlog

## Engineering Workflow

- Work from short-lived branches (`feat/*`, `fix/*`, `chore/*`)
- Open PRs early, keep scope focused
- Merge via squash after CI passes

Contribution details: [`CONTRIBUTING.md`](CONTRIBUTING.md)
