# poly-clob

[![CI](https://github.com/orestmx/poly-clob/actions/workflows/ci.yml/badge.svg)](https://github.com/orestmx/poly-clob/actions/workflows/ci.yml)

A central limit order book (CLOB) and matching engine written in modern C++ (C++20).

The goal is a fast, well-tested matching engine that can be used as a **simulation venue for
testing market-making bots**. It targets [Polymarket](https://polymarket.com) first, but the
core is exchange-agnostic so other venues can be added through thin adapters.

## Why this project

Order matching is the heart of every exchange, and getting it correct — price-time priority,
partial fills, order lifecycle — is subtle. This repo builds that engine from scratch, with an
emphasis on:

- **Correctness over cleverness** — unit tests plus a randomized fuzz test that checks
  invariants over millions of operations.
- **No floating-point money** — prices and quantities are plain integers, avoiding the rounding
  errors that come with using floats for anything financial.
- **A clean core/adapter split** — the engine knows nothing about any specific exchange.
- **Measured, not assumed, performance** — a benchmark harness reports real throughput and
  latency percentiles instead of guesswork.

## Design notes

- **Prices and quantities are integers.** Both are `int32_t` — exact arithmetic, no rounding
  errors. Tests and benchmarks currently treat them as abstract ticks (e.g. `1`-`99`); mapping
  that onto Polymarket's actual `(0, 1)` probability range is planned but not yet implemented.
- **Price-time priority.** Orders at a given price are matched first-in, first-out.
- **O(1) cancels.** An order-id lookup table (`id_map`) stores each order's position in its
  price level so cancellation does not require a scan.

## Architecture

```
clob/
├── CMakeLists.txt                      # CMake build (app + tests + optional bench)
├── Makefile                            # lightweight build for quick engine iteration
├── src/
│   ├── main.cpp                        # demo entry point
│   └── engine/
│       ├── Types.hpp                   # Price, Quantity, OrderId, Side, OrderType
│       ├── Order.hpp                   # Order and Trade structs
│       ├── OrderBook.hpp               # OrderBook interface
│       └── OrderBook.cpp               # matching engine implementation
├── tests/
│   ├── test_orderbook.cpp              # hand-written unit tests
│   ├── test_fuzz.cpp                   # randomized fuzz test (5 correctness invariants)
│   └── support/
│       └── OrderFlowGenerator.{hpp,cpp}  # seeded random order-flow generator
└── bench/
    └── bench_orderbook.cpp             # throughput + latency-percentile benchmark
```

Core concepts:

| Type        | Purpose                                                       |
|-------------|-----------------------------------------------------------------|
| `Order`     | A resting or incoming order (id, side, price, quantity, type)   |
| `Trade`     | The result of two orders matching                               |
| `OrderBook` | Holds bids/asks, matches incoming orders, manages cancels        |

**Order types**: Good-Till-Cancel, Immediate-Or-Cancel, Fill-Or-Kill, Market.

## Build & run

Requires a C++20 compiler (clang or gcc) and CMake >= 3.16.

### CMake (builds the app + tests; fetches GoogleTest automatically)

```bash
cmake -S clob -B clob/build                        # configure (downloads GoogleTest on first run)
cmake --build clob/build                           # build simulator + unit_tests
ctest --test-dir clob/build --output-on-failure    # run the test suite
./clob/build/simulator                             # run the demo
```

This is what CI runs, and it needs no system GoogleTest install.

**Fuzz test op count**: `test_fuzz.cpp` defaults to 200,000 operations so CI stays fast. For a
much more thorough run (e.g. after changing the matching engine itself), override it:

```bash
CLOB_FUZZ_OPS=1000000 ./clob/build/unit_tests --gtest_filter="Fuzz.*"
```

### Benchmark (Release build only — Debug numbers aren't meaningful)

```bash
cmake -S clob -B clob/build-rel -DCMAKE_BUILD_TYPE=Release -DCLOB_BUILD_BENCH=ON
cmake --build clob/build-rel --target bench
./clob/build-rel/bench
```

See [Benchmarks](#benchmarks) below for what it reports and current results.

### Make (quick iteration on the engine only — doesn't build the fuzz test or benchmark)

```bash
cd clob
make            # builds ./simulator
./simulator     # runs the demo
make test       # builds and runs the GoogleTest suite (requires GoogleTest installed locally)
make clean      # removes binaries and object files
```

## Testing strategy

Correctness is checked two ways:

- **Unit tests** (`tests/test_orderbook.cpp`) — hand-written cases covering matching, partial
  fills, FIFO ordering, and order-type-specific behavior (GTC/IOC/FOK/Market).
- **Fuzz testing** (`tests/test_fuzz.cpp`) — a seeded `OrderFlowGenerator` fires a long stream of
  random adds/cancels at the book, and after *every* operation the test checks 5 invariants:
  quantity conservation, the book is never crossed, the `id_map` stays in sync with the book,
  no "ghost" orders/empty price levels linger, and trade prices stay sane relative to both the
  taker's limit and the pre-trade spread. Same seed always reproduces the same run, so any
  failure hands you an exact reproducible case.

## Benchmarks

Measured on an Apple M1 (Release build, single dev machine — not an isolated benchmark box). Reproduce with the benchmark build
above; see `clob/bench/bench_orderbook.cpp` for the exact scenarios.

Throughput and latency at three different steady-state book depths:

| Book depth        | Combined       | `add_order`    | `cancel_order` | p50      | p99      | p99.9    |
|--------------------|---------------:|---------------:|---------------:|---------:|---------:|---------:|
| ~25K resting orders | 7.00M ops/sec  | 7.16M ops/sec  | 8.44M ops/sec  | 0.125 us | 0.458 us | 0.680 us  |
| ~100K resting orders| 6.24M ops/sec  | 6.39M ops/sec  | 4.04M ops/sec  | 0.125 us | 0.530 us | 0.850 us  |
| ~250K resting orders| 5.49M ops/sec  | 5.90M ops/sec  | 3.12M ops/sec  | 0.125 us | 0.583 us | 0.860 us  |

Run-to-run variance across different random seeds at the same depth is small (~1-4% of the
mean), so these trends are reliable rather than noise.

Two takeaways:

- **Throughput and tail latency both degrade smoothly as the book gets deeper** — a 10x deeper
  book (25K -> 250K resting orders) costs about 22% combined throughput and roughly doubles
  p99.9 latency.
- **`cancel_order` starts faster than `add_order` but crosses over to slower** somewhere between
  ~25K and ~100K resting orders, and stays slower at greater depth. Leading hypothesis is
  cache-locality pressure on `id_map`/`std::map` as their memory footprint outgrows cache; still
  under investigation (profiling with real cache-miss counters on Linux).

## Roadmap

- [x] **Core matching engine** — price-time priority, partial fills, GTC orders.
- [x] **All order types** — IOC, FOK, Market.
- [x] **Cancel** orders via the id lookup table.
- [x] **Unit test suite** (GoogleTest) covering matching, partial fills, FIFO ordering, and edge
  cases.
- [x] **Fuzz testing** — randomized order flow checked against 5 correctness invariants, passing
  1M+ operations.
- [x] **CMake build and CI** (GitHub Actions: build + test on push, gcc & clang).
- [x] **Benchmarks** — throughput (combined/add/cancel) and latency percentiles across varying
  book depths.

Exchange adapters, a market-making backtest harness, and open performance questions are tracked
internally and not yet built.
