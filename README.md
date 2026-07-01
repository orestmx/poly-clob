# poly-clob

A central limit order book (CLOB) and matching engine written in modern C++ (C++20).

The goal is a fast, well-tested matching engine that can be used as a **simulation venue for testing market-making bots**. It targets [Polymarket](https://polymarket.com) first, but the core is exchange-agnostic so other venues can be added through thin adapters.

## Why this project

Order matching is the heart of every exchange, and getting it correct — price-time priority, partial fills, order lifecycle — is subtle. This repo builds that engine from scratch, with an emphasis on:

- **Correctness over cleverness** — extensive unit tests for the matching logic.
- **No floating-point money** — prices and quantities are fixed-point integers, the standard approach in trading systems to avoid rounding errors.
- **A clean core/adapter split** — the engine knows nothing about any specific exchange.

## Design notes

- **Prices and quantities are integers.** Prices are stored as fixed-point micro-units (`$1.00` = `1_000_000`), so all arithmetic is exact. Polymarket prices live in `(0, 1)`, which maps cleanly onto this representation.
- **Price-time priority.** Orders at a given price are matched first-in, first-out.
- **O(1) cancels.** An order-id lookup table stores each order's position in its price level so cancellation does not require a scan.

## Architecture

```
clob/
└── src/
    ├── main.cpp              # demo entry point
    └── engine/
        ├── Types.hpp         # Price, Quantity, OrderId, Side, OrderType
        ├── Order.hpp         # Order and Trade structs
        ├── OrderBook.hpp     # OrderBook interface
        └── OrderBook.cpp     # matching engine implementation
```

Core concepts:

| Type        | Purpose                                                       |
|-------------|---------------------------------------------------------------|
| `Order`     | A resting or incoming order (id, side, price, quantity, type) |
| `Trade`     | The result of two orders matching                             |
| `OrderBook` | Holds bids/asks, matches incoming orders, manages cancels     |

**Order types** (target): Good-Till-Cancel, Immediate-Or-Cancel, Fill-Or-Kill, Market.

## Build & run

Requires a C++20 compiler (clang or gcc).

```bash
cd clob
make            # builds ./simulator (incremental — only rebuilds changed files)
./simulator     # runs the test suite
make clean      # removes the binary and object files
```

<details>
<summary>Building without make</summary>

```bash
cd clob
clang++ -std=c++20 -O3 -Wall -Wextra \
  src/main.cpp src/engine/OrderBook.cpp -I src -o simulator
```
</details>

> A proper CMake build is planned (see roadmap).

## Roadmap

- [x] **Core matching engine** — price-time priority, partial fills, GTC orders.
- [x] **All order types** — IOC, FOK, Market.
- [x] **Cancel / modify** orders via the id lookup table.
- [ ] **Unit test suite** (GoogleTest) covering matching, partial fills, FIFO ordering, and edge cases.
- [ ] **CMake build** and CI (GitHub Actions: build + test on push).
- [ ] **Exchange-agnostic feed interface** — a `MarketDataSource` abstraction.
- [ ] **Polymarket adapter** — replay recorded L2 data into the engine.
- [ ] **Market-making test harness** — a `Strategy` interface and backtest loop with PnL / inventory / fill metrics.
- [ ] **Benchmarks** — orders per second.
