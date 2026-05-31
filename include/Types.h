#pragma once
#include <cstdint>
#include <string>

namespace ob {

using OrderId  = uint64_t;
using Price    = int64_t;   // price in ticks (integer to avoid float precision issues)
using Quantity = uint64_t;
using Timestamp = uint64_t; // nanoseconds (RDTSC)

enum class Side : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t { LIMIT, MARKET, IOC, FOK };
enum class OrderStatus : uint8_t { NEW, PARTIAL, FILLED, CANCELLED, REJECTED };

struct Order {
    OrderId   id;
    Price     price;
    Quantity  qty;
    Quantity  filled_qty  = 0;
    Side      side;
    OrderType type;
    OrderStatus status    = OrderStatus::NEW;
    Timestamp   timestamp = 0;

    Quantity remaining() const noexcept { return qty - filled_qty; }
    bool     is_done()   const noexcept {
        return status == OrderStatus::FILLED ||
               status == OrderStatus::CANCELLED ||
               status == OrderStatus::REJECTED;
    }
};

struct Trade {
    OrderId  aggressor_id;
    OrderId  passive_id;
    Price    price;
    Quantity qty;
    Timestamp timestamp;
};

} // namespace ob
