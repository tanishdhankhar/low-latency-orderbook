#include "MatchingEngine.h"
#include "Latency.h"
#include <cstdio>
#include <chrono>
#include <memory>

using namespace ob;

int main() {
    TscClock::calibrate();
    printf("TSC calibrated: %.3f ticks/ns\n\n", TscClock::ticks_per_ns);

    // ── Throughput benchmark ──────────────────────────────────────────────
    {
        constexpr int N = 1'000'000;
        std::vector<Trade> trades;
        trades.reserve(N / 2);
        OrderBook book([&](const Trade& t){ trades.push_back(t); });

        std::vector<Order> orders(N);
        for (int i = 0; i < N; ++i) {
            orders[i] = Order{
                (OrderId)(i + 1),
                (Price)(100 + (i % 50)),
                10, 0,
                (i % 2 == 0 ? Side::BUY : Side::SELL),
                OrderType::LIMIT,
                OrderStatus::NEW, 0
            };
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i)
            book.add_order(orders[i]);
        auto t1 = std::chrono::high_resolution_clock::now();

        double seconds    = std::chrono::duration<double>(t1 - t0).count();
        double ops_per_sec = N / seconds;

        printf("══════════════════════════════════════════════\n");
        printf("  THROUGHPUT BENCHMARK\n");
        printf("  Orders processed : %d\n", N);
        printf("  Time elapsed     : %.3f ms\n", seconds * 1000);
        printf("  Throughput       : %.0f orders/sec (%.2fM/sec)\n",
               ops_per_sec, ops_per_sec / 1e6);
        printf("  Trades generated : %zu\n", trades.size());
        printf("══════════════════════════════════════════════\n\n");
    }

    // ── Per-order latency benchmark ──────────────────────────────────────
    {
        constexpr int N = 100'000;
        // Heap-allocate engine (61 MB — too large for stack)
        auto engine = std::make_unique<MatchingEngine>();
        LatencyStats stats;

        for (int i = 0; i < N; ++i) {
            const uint64_t t0 = rdtsc();
            engine->process_one(OrderRequest{
                OrderRequest::Op::ADD,
                (OrderId)(i + 1),
                100,
                10,
                (i % 2 == 0 ? Side::BUY : Side::SELL),
                OrderType::LIMIT
            });
            const uint64_t t1 = rdtsc();
            stats.record(t0, t1);
        }

        stats.print("ORDER PROCESSING LATENCY");
    }

    // ── Cancel latency benchmark ─────────────────────────────────────────
    {
        constexpr int N = 50'000;
        std::vector<Order> orders(N);
        OrderBook book;
        LatencyStats cancel_stats;

        for (int i = 0; i < N; ++i) {
            orders[i] = Order{
                (OrderId)(i + 1),
                (i % 2 == 0 ? (Price)90 : (Price)110),
                10, 0,
                (i % 2 == 0 ? Side::BUY : Side::SELL),
                OrderType::LIMIT,
                OrderStatus::NEW, 0
            };
            book.add_order(orders[i]);
        }

        for (int i = 0; i < N; ++i) {
            const uint64_t t0 = rdtsc();
            book.cancel_order(i + 1);
            const uint64_t t1 = rdtsc();
            cancel_stats.record(t0, t1);
        }

        cancel_stats.print("CANCEL ORDER LATENCY");
    }

    return 0;
}
