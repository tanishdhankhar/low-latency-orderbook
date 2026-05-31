#pragma once
#include "Types.h"
#include "ObjectPool.h"
#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <functional>
#include <stdexcept>

namespace ob {

static constexpr std::size_t MAX_ORDERS = 1 << 20; // 1M orders in pool

/**
 * Limit Order Book
 *
 * Structure:
 *   Bids: std::map<Price, PriceLevel> in descending order (highest bid first)
 *   Asks: std::map<Price, PriceLevel> in ascending  order (lowest ask first)
 *
 * Each PriceLevel holds a doubly-linked list of orders (FIFO priority).
 * Order lookup by ID is O(1) via unordered_map.
 * Insert/cancel: O(log P) where P = number of distinct price levels.
 * Match (top-of-book): O(1) per trade.
 */

struct PriceLevel {
    Price           price;
    Quantity        total_qty = 0;
    std::list<Order*> orders; // FIFO
};

using TradeCallback = std::function<void(const Trade&)>;

class OrderBook {
public:
    explicit OrderBook(TradeCallback cb = nullptr)
        : on_trade_(std::move(cb)) {}

    // ── Public API ──────────────────────────────────────────────────

    // Add a new order. Returns filled quantity (0 for resting limit orders).
    Quantity add_order(Order& order);

    // Cancel an existing order by ID. Returns false if not found.
    bool cancel_order(OrderId id);

    // Modify price/qty of a resting order (cancel + re-insert).
    bool modify_order(OrderId id, Price new_price, Quantity new_qty);

    // ── Book queries ────────────────────────────────────────────────

    // Best bid (highest buy price). Returns 0 if no bids.
    Price best_bid() const noexcept {
        return bids_.empty() ? 0 : bids_.begin()->first;
    }

    // Best ask (lowest sell price). Returns 0 if no asks.
    Price best_ask() const noexcept {
        return asks_.empty() ? 0 : asks_.begin()->first;
    }

    int64_t spread() const noexcept {
        if (bids_.empty() || asks_.empty()) return -1;
        return best_ask() - best_bid();
    }

    Quantity bid_qty_at(Price p) const noexcept {
        auto it = bids_.find(p);
        return (it != bids_.end()) ? it->second.total_qty : 0;
    }

    Quantity ask_qty_at(Price p) const noexcept {
        auto it = asks_.find(p);
        return (it != asks_.end()) ? it->second.total_qty : 0;
    }

    std::size_t order_count() const noexcept { return order_map_.size(); }
    std::size_t bid_levels()  const noexcept { return bids_.size(); }
    std::size_t ask_levels()  const noexcept { return asks_.size(); }

    // Top N levels for each side (for market data snapshot)
    struct Level { Price price; Quantity qty; };
    std::vector<Level> top_bids(std::size_t n = 5) const;
    std::vector<Level> top_asks(std::size_t n = 5) const;

    // Self-trade prevention: set the trader ID on orders to use
    void set_self_trade_prevention(bool enabled) { stp_ = enabled; }

    // Print book state (debugging / integration tests)
    void print(std::size_t levels = 5) const;

private:
    // Descending bids (best = begin), ascending asks (best = begin)
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>>    asks_;

    // O(1) order lookup
    std::unordered_map<OrderId, Order*> order_map_;

    TradeCallback on_trade_;
    bool stp_ = true;

    // ── Matching logic ──────────────────────────────────────────────

    template<typename BookSide>
    Quantity match(Order& aggressor, BookSide& passive_side);

    void insert_to_book(Order& order);
    void remove_from_level(Order& order, PriceLevel& level,
                           bool is_bid);
};

// ── Implementation ───────────────────────────────────────────────────────────

inline Quantity OrderBook::add_order(Order& order) {
    if (order.qty == 0) {
        order.status = OrderStatus::REJECTED;
        return 0;
    }

    Quantity filled = 0;

    if (order.type == OrderType::MARKET) {
        // Market order: match against opposite side at any price
        if (order.side == Side::BUY)
            filled = match(order, asks_);
        else
            filled = match(order, bids_);

        if (order.remaining() > 0)
            order.status = OrderStatus::CANCELLED; // unfilled market qty cancelled
        return filled;
    }

    // Limit / IOC / FOK
    if (order.side == Side::BUY) {
        // Buy: match against asks where ask_price <= order_price
        filled = match(order, asks_);
    } else {
        // Sell: match against bids where bid_price >= order_price
        filled = match(order, bids_);
    }

    if (order.type == OrderType::IOC) {
        // Immediate-or-cancel: cancel any unfilled remainder
        if (order.remaining() > 0)
            order.status = OrderStatus::CANCELLED;
        return filled;
    }

    if (order.type == OrderType::FOK) {
        // Fill-or-kill: if not completely filled, reject entire order
        if (order.remaining() > 0) {
            // Undo partial fills (simplified: mark rejected, no undo here;
            // a real system would need a rollback mechanism)
            order.status = OrderStatus::REJECTED;
        }
        return filled;
    }

    // Resting limit order
    if (order.remaining() > 0) {
        insert_to_book(order);
        order_map_[order.id] = &order;
    }

    return filled;
}

inline bool OrderBook::cancel_order(OrderId id) {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;

    Order* o = it->second;
    o->status = OrderStatus::CANCELLED;

    if (o->side == Side::BUY) {
        auto lit = bids_.find(o->price);
        if (lit != bids_.end())
            remove_from_level(*o, lit->second, true);
        if (lit != bids_.end() && lit->second.orders.empty())
            bids_.erase(lit);
    } else {
        auto lit = asks_.find(o->price);
        if (lit != asks_.end())
            remove_from_level(*o, lit->second, false);
        if (lit != asks_.end() && lit->second.orders.empty())
            asks_.erase(lit);
    }

    order_map_.erase(it);
    return true;
}

inline bool OrderBook::modify_order(OrderId id, Price new_price, Quantity new_qty) {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;
    Order* o = it->second;

    // Simple cancel + re-add (loses time priority — acceptable for this impl)
    Side      side = o->side;
    OrderType type = o->type;

    cancel_order(id);

    o->price  = new_price;
    o->qty    = new_qty;
    o->filled_qty = 0;
    o->status = OrderStatus::NEW;
    o->side   = side;
    o->type   = type;

    add_order(*o);
    return true;
}

template<typename BookSide>
inline Quantity OrderBook::match(Order& aggressor, BookSide& passive_side) {
    Quantity total_filled = 0;

    while (aggressor.remaining() > 0 && !passive_side.empty()) {
        auto& [level_price, level] = *passive_side.begin();

        // Price check for limit orders
        if (aggressor.type == OrderType::LIMIT ||
            aggressor.type == OrderType::IOC   ||
            aggressor.type == OrderType::FOK) {
            if (aggressor.side == Side::BUY  && level_price > aggressor.price) break;
            if (aggressor.side == Side::SELL && level_price < aggressor.price) break;
        }

        // Walk orders at this level (FIFO)
        while (aggressor.remaining() > 0 && !level.orders.empty()) {
            Order* passive = level.orders.front();

            // Self-trade prevention
            if (stp_ && passive->id == aggressor.id) {
                aggressor.status = OrderStatus::CANCELLED;
                return total_filled;
            }

            const Quantity trade_qty = std::min(aggressor.remaining(),
                                                passive->remaining());

            aggressor.filled_qty += trade_qty;
            passive->filled_qty  += trade_qty;
            level.total_qty      -= trade_qty;
            total_filled         += trade_qty;

            if (on_trade_) {
                on_trade_(Trade{
                    aggressor.id, passive->id,
                    level_price, trade_qty, aggressor.timestamp
                });
            }

            if (passive->remaining() == 0) {
                passive->status = OrderStatus::FILLED;
                order_map_.erase(passive->id);
                level.orders.pop_front();
            } else {
                passive->status = OrderStatus::PARTIAL;
            }
        }

        if (level.orders.empty())
            passive_side.erase(passive_side.begin());
    }

    if (aggressor.filled_qty > 0 && aggressor.remaining() == 0)
        aggressor.status = OrderStatus::FILLED;
    else if (aggressor.filled_qty > 0)
        aggressor.status = OrderStatus::PARTIAL;

    return total_filled;
}

inline void OrderBook::insert_to_book(Order& order) {
    if (order.side == Side::BUY) {
        auto& level = bids_[order.price];
        level.price = order.price;
        level.total_qty += order.remaining();
        level.orders.push_back(&order);
    } else {
        auto& level = asks_[order.price];
        level.price = order.price;
        level.total_qty += order.remaining();
        level.orders.push_back(&order);
    }
}

inline void OrderBook::remove_from_level(Order& order, PriceLevel& level, bool /*is_bid*/) {
    for (auto it = level.orders.begin(); it != level.orders.end(); ++it) {
        if ((*it)->id == order.id) {
            level.total_qty -= order.remaining();
            level.orders.erase(it);
            return;
        }
    }
}

inline std::vector<OrderBook::Level> OrderBook::top_bids(std::size_t n) const {
    std::vector<Level> result;
    result.reserve(n);
    for (auto& [p, lvl] : bids_) {
        if (result.size() >= n) break;
        result.push_back({p, lvl.total_qty});
    }
    return result;
}

inline std::vector<OrderBook::Level> OrderBook::top_asks(std::size_t n) const {
    std::vector<Level> result;
    result.reserve(n);
    for (auto& [p, lvl] : asks_) {
        if (result.size() >= n) break;
        result.push_back({p, lvl.total_qty});
    }
    return result;
}

inline void OrderBook::print(std::size_t levels) const {
    printf("\n╔══════════════════════════════╗\n");
    printf("║         ORDER BOOK           ║\n");
    printf("╠══════════════════════════════╣\n");

    auto asks = top_asks(levels);
    for (auto it = asks.rbegin(); it != asks.rend(); ++it)
        printf("║  ASK  %8ld  x  %8lu  ║\n", (long)it->price, (unsigned long)it->qty);

    if (!bids_.empty() && !asks_.empty()) {
        printf("║ ─── spread: %-4ld ────────── ║\n", (long)spread());
    }

    for (auto& l : top_bids(levels))
        printf("║  BID  %8ld  x  %8lu  ║\n", (long)l.price, (unsigned long)l.qty);

    printf("╚══════════════════════════════╝\n\n");
}

} // namespace ob
