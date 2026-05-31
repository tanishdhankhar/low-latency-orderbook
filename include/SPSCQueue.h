#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>

namespace ob {

/**
 * Lock-free Single-Producer Single-Consumer ring buffer.
 *
 * Design:
 *  - Head and tail are on separate cache lines to eliminate false sharing.
 *  - Uses relaxed/release/acquire atomics for minimal memory barriers.
 *  - Capacity must be a power of two for fast modulo via bitwise AND.
 *  - Zero dynamic allocation after construction.
 */
template<typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    static constexpr std::size_t CACHE_LINE = 64;
    static constexpr std::size_t MASK       = Capacity - 1;

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Producer side — returns false if queue is full
    bool push(const T& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & MASK;
        if (next == head_.load(std::memory_order_acquire))
            return false; // full
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool push(T&& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & MASK;
        if (next == head_.load(std::memory_order_acquire))
            return false;
        buffer_[tail] = std::move(item);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side — returns empty optional if queue is empty
    std::optional<T> pop() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return std::nullopt; // empty
        T item = std::move(buffer_[head]);
        head_.store((head + 1) & MASK, std::memory_order_release);
        return item;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    std::size_t size() const noexcept {
        const std::size_t t = tail_.load(std::memory_order_acquire);
        const std::size_t h = head_.load(std::memory_order_acquire);
        return (t - h + Capacity) & MASK;
    }

private:
    alignas(CACHE_LINE) std::atomic<std::size_t> head_;
    alignas(CACHE_LINE) std::atomic<std::size_t> tail_;
    std::array<T, Capacity> buffer_;
};

} // namespace ob
