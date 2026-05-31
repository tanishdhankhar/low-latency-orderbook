#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <time.h>

namespace ob {

// Read CPU timestamp counter (nanosecond-precision on modern x86)
inline uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// Approximate TSC ticks → nanoseconds (calibrate on startup)
struct TscClock {
    static double ticks_per_ns;

    static void calibrate() {
        // Busy-wait ~10ms and measure
        struct timespec t1, t2;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t c1 = rdtsc();
        // spin 10ms
        struct timespec t2b;
        do { clock_gettime(CLOCK_MONOTONIC, &t2b); }
        while ((t2b.tv_sec - t1.tv_sec) * 1'000'000'000LL +
               (t2b.tv_nsec - t1.tv_nsec) < 10'000'000LL);
        uint64_t c2 = rdtsc();
        clock_gettime(CLOCK_MONOTONIC, &t2);
        double ns = (t2.tv_sec - t1.tv_sec) * 1e9 + (t2.tv_nsec - t1.tv_nsec);
        ticks_per_ns = (c2 - c1) / ns;
    }

    static double to_ns(uint64_t ticks) noexcept {
        return ticks / ticks_per_ns;
    }
};

inline double TscClock::ticks_per_ns = 3.0; // default ~3 GHz; calibrate() updates it

struct LatencyStats {
    std::vector<double> samples;

    void record(uint64_t start, uint64_t end) {
        samples.push_back(TscClock::to_ns(end - start));
    }

    void print(const char* label) const {
        if (samples.empty()) { printf("%s: no data\n", label); return; }

        std::vector<double> s = samples;
        std::sort(s.begin(), s.end());

        double sum  = std::accumulate(s.begin(), s.end(), 0.0);
        double mean = sum / s.size();

        auto pct = [&](double p) -> double {
            std::size_t idx = static_cast<std::size_t>(p / 100.0 * s.size());
            if (idx >= s.size()) idx = s.size() - 1;
            return s[idx];
        };

        printf("─────────────────────────────────────────\n");
        printf("  %s  (n=%zu)\n", label, s.size());
        printf("  min:  %8.1f ns\n", s.front());
        printf("  mean: %8.1f ns\n", mean);
        printf("  p50:  %8.1f ns\n", pct(50));
        printf("  p90:  %8.1f ns\n", pct(90));
        printf("  p99:  %8.1f ns\n", pct(99));
        printf("  p999: %8.1f ns\n", pct(99.9));
        printf("  max:  %8.1f ns\n", s.back());
        printf("─────────────────────────────────────────\n");
    }
};

} // namespace ob
