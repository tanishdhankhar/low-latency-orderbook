#include <gtest/gtest.h>
#include "OrderBook.h"
#include "SPSCQueue.h"
#include "MatchingEngine.h"
#include <vector>
#include <thread>

using namespace ob;

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

static Order make_order(OrderId id, Side side, Price price, Quantity qty,
                        OrderType type = OrderType::LIMIT) {
    return Order{id, price, qty, 0, side, type, OrderStatus::NEW, 0};
}

// ────────────────────────────────────────────────────────────────────────────
// 1. Basic limit order resting
// ────────────────────────────────────────────────────────────────────────────

TEST(BasicOrders, RestingBuyOrder) {
    OrderBook book;
    auto o = make_order(1, Side::BUY, 100, 10);
    book.add_order(o);
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.bid_qty_at(100), 10u);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(BasicOrders, RestingSellOrder) {
    OrderBook book;
    auto o = make_order(1, Side::SELL, 105, 5);
    book.add_order(o);
    EXPECT_EQ(book.best_ask(), 105);
    EXPECT_EQ(book.ask_qty_at(105), 5u);
}

TEST(BasicOrders, NoBestBidWhenEmpty) {
    OrderBook book;
    EXPECT_EQ(book.best_bid(), 0);
    EXPECT_EQ(book.best_ask(), 0);
}

TEST(BasicOrders, MultiplePriceLevels) {
    OrderBook book;
    for (Price p : {100, 99, 98}) {
        auto o = make_order(p, Side::BUY, p, 10);
        book.add_order(o);
    }
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.bid_levels(), 3u);
}

// ────────────────────────────────────────────────────────────────────────────
// 2. Full match
// ────────────────────────────────────────────────────────────────────────────

TEST(Matching, FullMatchSamePriceQty) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto bid = make_order(1, Side::BUY,  100, 10);
    auto ask = make_order(2, Side::SELL, 100, 10);
    book.add_order(bid);
    book.add_order(ask);

    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, 10u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_EQ(bid.status, OrderStatus::FILLED);
    EXPECT_EQ(ask.status, OrderStatus::FILLED);
}

TEST(Matching, PriceImprovement) {
    // Passive bid at 100, aggressive sell at 95 → matches at 100 (passive price)
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto bid = make_order(1, Side::BUY,  100, 10);
    auto ask = make_order(2, Side::SELL,  95,  10);
    book.add_order(bid);
    book.add_order(ask);

    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, 100); // executes at passive (bid) price
}

// ────────────────────────────────────────────────────────────────────────────
// 3. Partial fills
// ────────────────────────────────────────────────────────────────────────────

TEST(Matching, PartialFillAggressor) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto bid = make_order(1, Side::BUY,  100, 5);
    auto ask = make_order(2, Side::SELL, 100, 10);
    book.add_order(bid);
    book.add_order(ask);

    EXPECT_EQ(trades[0].qty, 5u);
    EXPECT_EQ(bid.status, OrderStatus::FILLED);
    EXPECT_EQ(ask.status, OrderStatus::PARTIAL);
    EXPECT_EQ(ask.remaining(), 5u);
    EXPECT_EQ(book.ask_qty_at(100), 5u);
}

TEST(Matching, PartialFillPassive) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto bid = make_order(1, Side::BUY,  100, 10);
    auto ask = make_order(2, Side::SELL, 100, 4);
    book.add_order(bid);
    book.add_order(ask);

    EXPECT_EQ(trades[0].qty, 4u);
    EXPECT_EQ(bid.status, OrderStatus::PARTIAL);
    EXPECT_EQ(bid.remaining(), 6u);
}

TEST(Matching, SweepMultipleLevels) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    // Three asks at different prices
    auto a1 = make_order(1, Side::SELL, 100, 5);
    auto a2 = make_order(2, Side::SELL, 101, 5);
    auto a3 = make_order(3, Side::SELL, 102, 5);
    book.add_order(a1);
    book.add_order(a2);
    book.add_order(a3);

    // Large buy order sweeps all three
    auto big_bid = make_order(4, Side::BUY, 105, 15);
    book.add_order(big_bid);

    EXPECT_EQ(trades.size(), 3u);
    EXPECT_EQ(big_bid.status, OrderStatus::FILLED);
    EXPECT_EQ(book.ask_levels(), 0u);
}

// ────────────────────────────────────────────────────────────────────────────
// 4. Cancel orders
// ────────────────────────────────────────────────────────────────────────────

TEST(Cancel, CancelRestingOrder) {
    OrderBook book;
    auto o = make_order(1, Side::BUY, 100, 10);
    book.add_order(o);
    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_EQ(book.best_bid(), 0);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(Cancel, CancelNonExistentOrder) {
    OrderBook book;
    EXPECT_FALSE(book.cancel_order(999));
}

TEST(Cancel, CancelPartiallyFilledOrder) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto bid = make_order(1, Side::BUY,  100, 10);
    auto ask = make_order(2, Side::SELL, 100, 3);
    book.add_order(bid);
    book.add_order(ask);

    EXPECT_EQ(bid.remaining(), 7u);
    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_EQ(book.bid_qty_at(100), 0u);
}

TEST(Cancel, CancelDoesNotAffectOtherLevel) {
    OrderBook book;
    auto o1 = make_order(1, Side::BUY, 100, 10);
    auto o2 = make_order(2, Side::BUY, 99,  5);
    book.add_order(o1);
    book.add_order(o2);
    book.cancel_order(1);
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.bid_qty_at(99), 5u);
}

// ────────────────────────────────────────────────────────────────────────────
// 5. FIFO priority at same price level
// ────────────────────────────────────────────────────────────────────────────

TEST(Priority, FIFOAtSameLevel) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto b1 = make_order(1, Side::BUY, 100, 5);
    auto b2 = make_order(2, Side::BUY, 100, 5);
    book.add_order(b1);
    book.add_order(b2);

    auto ask = make_order(3, Side::SELL, 100, 5);
    book.add_order(ask);

    // First order entered (b1) should be filled first
    EXPECT_EQ(trades[0].passive_id, 1u);
    EXPECT_EQ(b1.status, OrderStatus::FILLED);
    EXPECT_EQ(b2.status, OrderStatus::NEW); // b2 untouched
}

// ────────────────────────────────────────────────────────────────────────────
// 6. Market orders
// ────────────────────────────────────────────────────────────────────────────

TEST(MarketOrder, MarketBuyMatchesBestAsk) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto ask = make_order(1, Side::SELL, 105, 10);
    book.add_order(ask);

    auto mkt = make_order(2, Side::BUY, 0, 5, OrderType::MARKET);
    book.add_order(mkt);

    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, 5u);
    EXPECT_EQ(mkt.status, OrderStatus::FILLED);
}

TEST(MarketOrder, MarketOrderCancelledIfNoLiquidity) {
    OrderBook book;
    auto mkt = make_order(1, Side::BUY, 0, 10, OrderType::MARKET);
    book.add_order(mkt);
    EXPECT_EQ(mkt.status, OrderStatus::CANCELLED);
}

// ────────────────────────────────────────────────────────────────────────────
// 7. IOC orders
// ────────────────────────────────────────────────────────────────────────────

TEST(IOC, FullyFilled) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto ask = make_order(1, Side::SELL, 100, 10);
    book.add_order(ask);

    auto ioc = make_order(2, Side::BUY, 100, 5, OrderType::IOC);
    book.add_order(ioc);

    EXPECT_EQ(ioc.status, OrderStatus::FILLED);
    EXPECT_EQ(book.order_count(), 1u); // ask partially remains
}

TEST(IOC, UnfilledPortionCancelled) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto ask = make_order(1, Side::SELL, 100, 3);
    book.add_order(ask);

    // IOC wants 10 but only 3 available
    auto ioc = make_order(2, Side::BUY, 100, 10, OrderType::IOC);
    book.add_order(ioc);

    EXPECT_EQ(ioc.filled_qty, 3u);
    EXPECT_EQ(ioc.status, OrderStatus::CANCELLED);
    // IOC should NOT rest in book
    EXPECT_EQ(book.bid_qty_at(100), 0u);
}

TEST(IOC, NoMatchCancelled) {
    OrderBook book;
    auto ioc = make_order(1, Side::BUY, 100, 10, OrderType::IOC);
    book.add_order(ioc);
    EXPECT_EQ(ioc.status, OrderStatus::CANCELLED);
    EXPECT_EQ(book.order_count(), 0u);
}

// ────────────────────────────────────────────────────────────────────────────
// 8. FOK orders
// ────────────────────────────────────────────────────────────────────────────

TEST(FOK, RejectedWhenInsufficientLiquidity) {
    OrderBook book;
    auto ask = make_order(1, Side::SELL, 100, 5);
    book.add_order(ask);

    auto fok = make_order(2, Side::BUY, 100, 10, OrderType::FOK);
    book.add_order(fok);
    EXPECT_EQ(fok.status, OrderStatus::REJECTED);
}

// ────────────────────────────────────────────────────────────────────────────
// 9. Crossed orders (buy price > ask price)
// ────────────────────────────────────────────────────────────────────────────

TEST(CrossedOrders, CrossedBookMatchesAtPassivePrice) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });

    auto ask = make_order(1, Side::SELL, 100, 10);
    book.add_order(ask);

    // Buy order with price well above ask — should match at ask price
    auto bid = make_order(2, Side::BUY, 110, 10);
    book.add_order(bid);

    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(book.spread(), -1); // empty book
}

// ────────────────────────────────────────────────────────────────────────────
// 10. Self-trade prevention
// ────────────────────────────────────────────────────────────────────────────

TEST(STP, SameIdNotMatched) {
    std::vector<Trade> trades;
    OrderBook book([&](const Trade& t){ trades.push_back(t); });
    book.set_self_trade_prevention(true);

    // Same ID on both sides — should not trade
    auto bid = make_order(42, Side::BUY,  100, 10);
    auto ask = make_order(42, Side::SELL, 100, 10);
    book.add_order(bid);
    book.add_order(ask);

    EXPECT_EQ(trades.size(), 0u);
}

// ────────────────────────────────────────────────────────────────────────────
// 11. Modify order
// ────────────────────────────────────────────────────────────────────────────

TEST(Modify, ChangePriceAndQty) {
    OrderBook book;
    auto o = make_order(1, Side::BUY, 100, 10);
    book.add_order(o);
    EXPECT_TRUE(book.modify_order(1, 105, 20));
    EXPECT_EQ(book.best_bid(), 105);
    EXPECT_EQ(book.bid_qty_at(100), 0u);
}

TEST(Modify, NonExistentOrder) {
    OrderBook book;
    EXPECT_FALSE(book.modify_order(999, 100, 10));
}

// ────────────────────────────────────────────────────────────────────────────
// 12. Zero-qty order rejected
// ────────────────────────────────────────────────────────────────────────────

TEST(Validation, ZeroQtyRejected) {
    OrderBook book;
    auto o = make_order(1, Side::BUY, 100, 0);
    book.add_order(o);
    EXPECT_EQ(o.status, OrderStatus::REJECTED);
    EXPECT_EQ(book.order_count(), 0u);
}

// ────────────────────────────────────────────────────────────────────────────
// 13. Top N levels snapshot
// ────────────────────────────────────────────────────────────────────────────

TEST(Snapshot, TopNLevels) {
    OrderBook book;
    for (int i = 0; i < 10; ++i) {
        auto o = make_order(i, Side::BUY, 100 - i, 10);
        book.add_order(o);
    }
    auto bids = book.top_bids(3);
    EXPECT_EQ(bids.size(), 3u);
    EXPECT_EQ(bids[0].price, 100);
    EXPECT_EQ(bids[1].price, 99);
    EXPECT_EQ(bids[2].price, 98);
}

// ────────────────────────────────────────────────────────────────────────────
// 14. SPSCQueue
// ────────────────────────────────────────────────────────────────────────────

TEST(SPSCQueue, PushPop) {
    SPSCQueue<int, 8> q;
    EXPECT_TRUE(q.push(42));
    auto v = q.pop();
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(SPSCQueue, EmptyReturnsNullopt) {
    SPSCQueue<int, 8> q;
    EXPECT_FALSE(q.pop().has_value());
}

TEST(SPSCQueue, FullReturnsFalse) {
    SPSCQueue<int, 4> q;
    // Capacity 4 → 3 usable slots (ring buffer leaves one empty)
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4)); // full
}

TEST(SPSCQueue, ConcurrentProducerConsumer) {
    SPSCQueue<int, 1024> q;
    constexpr int N = 500;
    std::vector<int> received;

    std::thread producer([&]{
        for (int i = 0; i < N; ++i) {
            while (!q.push(i)) {} // spin
        }
    });

    std::thread consumer([&]{
        int count = 0;
        while (count < N) {
            if (auto v = q.pop()) {
                received.push_back(*v);
                ++count;
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received.size(), (size_t)N);
    for (int i = 0; i < N; ++i)
        EXPECT_EQ(received[i], i);
}

// ────────────────────────────────────────────────────────────────────────────
// 15. Spread calculation
// ────────────────────────────────────────────────────────────────────────────

TEST(Spread, CorrectSpread) {
    OrderBook book;
    auto bid = make_order(1, Side::BUY,  100, 5);
    auto ask = make_order(2, Side::SELL, 103, 5);
    book.add_order(bid);
    book.add_order(ask);
    EXPECT_EQ(book.spread(), 3);
}

TEST(Spread, NegativeWhenOneSideEmpty) {
    OrderBook book;
    auto bid = make_order(1, Side::BUY, 100, 5);
    book.add_order(bid);
    EXPECT_EQ(book.spread(), -1);
}

// ────────────────────────────────────────────────────────────────────────────
// 16. Large-scale stress
// ────────────────────────────────────────────────────────────────────────────

TEST(Stress, OneMillion_AddCancel) {
    OrderBook book;
    constexpr int N = 100'000;
    std::vector<Order> orders(N);

    for (int i = 0; i < N; ++i) {
        orders[i] = make_order(i + 1, (i % 2 == 0 ? Side::BUY : Side::SELL),
                               100 + (i % 20), 10);
        book.add_order(orders[i]);
    }
    // Cancel all resting orders
    for (int i = 0; i < N; ++i)
        book.cancel_order(i + 1);

    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_EQ(book.bid_levels(), 0u);
    EXPECT_EQ(book.ask_levels(), 0u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
