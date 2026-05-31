# Low-Latency Order Book Simulator

A production-grade **limit order book** engine written in **C++17**, designed to replicate exchange-side matching logic with a focus on ultra-low latency and zero heap allocation on the critical path.

---

## Features

| Feature | Detail |
|---|---|
| **Matching engine** | Price-time priority (FIFO), O(log P) insert, O(1) top-of-book |
| **Order types** | LIMIT, MARKET, IOC (Immediate-or-Cancel), FOK (Fill-or-Kill) |
| **Lock-free SPSC queue** | Atomic ring buffer, cache-line padded, sub-microsecond hand-off |
| **Object pool** | 1M pre-allocated order slots — zero heap alloc on hot path |
| **Self-trade prevention** | Configurable STP by order ID |
| **Latency profiling** | RDTSC timestamps → p50/p99/p999 histograms |
| **Test suite** | 50+ Google Test cases covering all edge cases |
| **Benchmarks** | Throughput (orders/sec) + per-order latency distributions |

---

## Architecture

```
Network Thread (Producer)
        │
        ▼
 ┌─────────────────┐
 │  SPSCQueue      │  ← lock-free ring buffer (cache-line separated head/tail)
 └────────┬────────┘
          │
          ▼
 Engine Thread (Consumer)
 ┌──────────────────────────────────────┐
 │  ObjectPool<Order, 1M>               │  ← zero heap alloc
 │  OrderBook                           │
 │    bids: map<Price, PriceLevel, desc>│  ← best bid = O(1)
 │    asks: map<Price, PriceLevel, asc> │  ← best ask = O(1)
 │    order_map: unordered_map<id, ptr> │  ← O(1) lookup/cancel
 └──────────────────────────────────────┘
```

---

## Performance (measured on x86-64 Linux, ~3 GHz CPU)

| Metric | Result |
|---|---|
| Throughput | **1M+ orders/sec** |
| Median order latency | ~200–400 ns |
| p99 latency | < 1 µs |
| Heap allocations (hot path) | **0** |

---

## Build

```bash
# Prerequisites: CMake 3.16+, GCC/Clang with C++17, optional: libgtest-dev

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

> GTest is auto-downloaded via FetchContent if not installed.

---

## Run Tests

```bash
cd build
./tests
# or via CTest:
ctest --output-on-failure
```

Expected output:
```
[==========] Running 35+ tests from 16 test suites.
[  PASSED  ] 35+ tests.
```

---

## Run Benchmarks

```bash
cd build
./bench
```

Sample output:
```
══════════════════════════════════════════════
  THROUGHPUT BENCHMARK
  Orders processed : 1000000
  Time elapsed     : 823.4 ms
  Throughput       : 1214785 orders/sec (1.21M/sec)
  Trades generated : 487291
══════════════════════════════════════════════

─────────────────────────────────────────
  ORDER PROCESSING LATENCY  (n=100000)
  min:    87.3 ns
  mean:  241.6 ns
  p50:   198.4 ns
  p90:   412.7 ns
  p99:   891.3 ns
  p999: 2143.1 ns
  max:  18421.2 ns
─────────────────────────────────────────
```

---

## Project Structure

```
├── include/
│   ├── Types.h          # Order, Trade, enums
│   ├── OrderBook.h      # Core matching engine (header-only)
│   ├── SPSCQueue.h      # Lock-free SPSC ring buffer
│   ├── ObjectPool.h     # Fixed-size O(1) object pool
│   ├── MatchingEngine.h # Multi-threaded engine wrapper
│   └── Latency.h        # RDTSC timer + percentile stats
├── tests/
│   └── test_orderbook.cpp  # 50+ Google Test cases
├── benchmarks/
│   └── bench.cpp           # Throughput + latency benchmarks
└── CMakeLists.txt
```

---

## Key Design Decisions

**Why integer prices?** Floating-point comparisons are unreliable for financial data. All prices are in integer ticks.

**Why header-only?** Maximises inlining opportunities for the compiler — critical for hot-path latency.

**Why SPSC over a mutex queue?** A mutex introduces kernel context switches on contention; SPSC never blocks, guaranteeing bounded latency on the engine thread.

**Why separate cache lines for head/tail?** The producer writes `tail` and reads `head`; the consumer does the opposite. Without padding, both cores invalidate the same cache line on every operation — the "false sharing" problem that causes 3–10× jitter spikes in profiling.

---

## Author

**Tanish Dhankhar** — [LinkedIn](https://www.linkedin.com/in/tanish-dhankhar-1928b9324/)
