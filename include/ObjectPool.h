#pragma once
#include <array>
#include <cstddef>
#include <cassert>

namespace ob {

/**
 * Fixed-size object pool — O(1) alloc/free, zero heap allocation after init.
 *
 * Uses a free-list stored directly inside the unused object slots
 * (intrusive free list), so no extra memory overhead.
 */
template<typename T, std::size_t Capacity>
class ObjectPool {
public:
    ObjectPool() {
        // Build the free list: each slot points to the next
        for (std::size_t i = 0; i < Capacity - 1; ++i)
            free_list_[i] = i + 1;
        free_list_[Capacity - 1] = SENTINEL;
        head_ = 0;
        available_ = Capacity;
    }

    // Allocate a slot and return a pointer to it (placement-new by caller)
    T* allocate() noexcept {
        if (head_ == SENTINEL) return nullptr;
        const std::size_t idx = head_;
        head_ = free_list_[idx];
        --available_;
        return reinterpret_cast<T*>(&storage_[idx]);
    }

    // Return a slot to the pool
    void deallocate(T* ptr) noexcept {
        ptr->~T();
        const std::size_t idx = reinterpret_cast<Slot*>(ptr) - storage_.data();
        assert(idx < Capacity);
        free_list_[idx] = head_;
        head_ = idx;
        ++available_;
    }

    std::size_t available() const noexcept { return available_; }
    std::size_t capacity()  const noexcept { return Capacity; }

private:
    static constexpr std::size_t SENTINEL = ~std::size_t(0);

    using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;
    std::array<Slot, Capacity>         storage_;
    std::array<std::size_t, Capacity>  free_list_;
    std::size_t head_      = 0;
    std::size_t available_ = Capacity;
};

} // namespace ob
