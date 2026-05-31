#pragma once
#include "OrderBook.h"
#include "SPSCQueue.h"
#include "ObjectPool.h"
#include "Latency.h"
#include "Types.h"
#include <atomic>
#include <thread>
#include <functional>

namespace ob {

struct OrderRequest {
    enum class Op : uint8_t { ADD, CANCEL, MODIFY } op;
    OrderId  id;
    Price    price;
    Quantity qty;
    Side     side;
    OrderType type;
};

/**
 * MatchingEngine
 *
 * Producer (network thread) → SPSCQueue → Consumer (engine thread)
 *
 * The engine thread owns the OrderBook and ObjectPool — no locks needed.
 * Orders are allocated from the pool (zero heap alloc on hot path).
 */
class MatchingEngine {
public:
    static constexpr std::size_t QUEUE_SIZE = 1 << 16; // 64k slots
    static constexpr std::size_t POOL_SIZE  = 1 << 20; // 1M orders

    explicit MatchingEngine(TradeCallback cb = nullptr)
        : book_(std::move(cb)), running_(false) {}

    ~MatchingEngine() { stop(); }

    // ── Producer API (safe to call from any thread) ──────────────────

    bool submit(const OrderRequest& req) noexcept {
        return queue_.push(req);
    }

    // ── Lifecycle ────────────────────────────────────────────────────

    void start() {
        running_ = true;
        engine_thread_ = std::thread(&MatchingEngine::run, this);
    }

    void stop() {
        running_ = false;
        if (engine_thread_.joinable())
            engine_thread_.join();
    }

    // Direct (single-threaded) processing — for benchmarks / tests
    void process_one(const OrderRequest& req) {
        dispatch(req);
    }

    const OrderBook& book() const noexcept { return book_; }
    LatencyStats&    latency()    noexcept { return stats_; }

private:
    OrderBook   book_;
    ObjectPool<Order, POOL_SIZE> pool_;
    SPSCQueue<OrderRequest, QUEUE_SIZE> queue_;
    LatencyStats stats_;

    std::atomic<bool> running_;
    std::thread engine_thread_;

    void run() {
        while (running_.load(std::memory_order_relaxed)) {
            auto req = queue_.pop();
            if (req) dispatch(*req);
        }
        // Drain remaining
        while (auto req = queue_.pop()) dispatch(*req);
    }

    void dispatch(const OrderRequest& req) {
        const uint64_t t0 = rdtsc();

        switch (req.op) {
        case OrderRequest::Op::ADD: {
            Order* o = pool_.allocate();
            if (!o) return; // pool exhausted (shouldn't happen in practice)
            new (o) Order{
                req.id, req.price, req.qty, 0,
                req.side, req.type, OrderStatus::NEW, t0
            };
            book_.add_order(*o);
            if (o->is_done()) pool_.deallocate(o);
            break;
        }
        case OrderRequest::Op::CANCEL:
            book_.cancel_order(req.id);
            break;
        case OrderRequest::Op::MODIFY:
            book_.modify_order(req.id, req.price, req.qty);
            break;
        }

        stats_.record(t0, rdtsc());
    }
};

} // namespace ob
