#include "gtest/gtest.h"
#include <gtest/gtest.h>

#include "engine/OrderBook.hpp"

using namespace clob;

// An incoming order always starts fully unfilled (initial == remaining),
// so this helper keeps the tests readable.
static Order make(OrderId id, Side side, Price price, Quantity qty,
                  OrderType type = OrderType::GoodTillCancel) {
    return Order{id, side, price, qty, qty, type};
}

// ============================================================
// Book queries on an empty / resting book
// ============================================================

TEST(OrderBook, EmptyBookHasNoBestPrices) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 0);
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 0);
}

TEST(OrderBook, UnmatchedOrderRestsOnBook) {
    OrderBook book;
    auto trades = book.add_order(make(1, Side::Buy, 100, 10));

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 10);
}

TEST(OrderBook, BestPricesTrackTopOfBook) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 98, 10));
    book.add_order(make(2, Side::Buy, 99, 10));   // better bid
    book.add_order(make(3, Side::Sell, 102, 10));
    book.add_order(make(4, Side::Sell, 101, 10)); // better ask

    EXPECT_EQ(book.best_bid(), 99);    // highest bid
    EXPECT_EQ(book.best_ask(), 101);   // lowest ask
}

TEST(OrderBook, NoCrossNoTrade) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 99, 10));
    auto trades = book.add_order(make(2, Side::Sell, 101, 10));

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.best_ask(), 101);
}

// ============================================================
// Basic matching
// ============================================================

TEST(OrderBook, ExactFillRemovesPriceLevel) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    auto trades = book.add_order(make(2, Side::Buy, 100, 5)); // consumes it exactly

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_FALSE(book.best_ask().has_value());   // level gone
    EXPECT_FALSE(book.best_bid().has_value());   // nothing rested
}

TEST(OrderBook, IncomingSellPartiallyFillsRestingBid) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 100, 10));               // resting bid, 10
    auto trades = book.add_order(make(2, Side::Sell, 100, 4)); // taker sells 4

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 4);
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 6);   // 6 still resting
    EXPECT_FALSE(book.best_ask().has_value());       // taker fully filled
}

TEST(OrderBook, IncomingBuyPartiallyFillsRestingAsk) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 10));
    auto trades = book.add_order(make(2, Side::Buy, 100, 4));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 4);
    EXPECT_EQ(book.best_ask(), 100);
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 6);
}

TEST(OrderBook, TradeCarriesMakerTakerPriceAndTakerSide) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5)); // maker
    auto trades = book.add_order(make(2, Side::Buy, 100, 5)); // taker

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_id, 1);
    EXPECT_EQ(trades[0].taker_id, 2);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(trades[0].side, Side::Buy);   // recorded as the taker's side
}

// ============================================================
// Price-time priority
// ============================================================

TEST(OrderBook, FifoOrderingAtSamePrice) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));  // arrived first
    book.add_order(make(2, Side::Sell, 100, 5));  // arrived second
    auto trades = book.add_order(make(3, Side::Buy, 100, 5));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_id, 1);             // first-in fills first
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 5); // order 2 still resting
}

TEST(OrderBook, AggressiveOrderTradesAtRestingPrice) {
    // Price improvement: a buy willing to pay 105 fills against a resting
    // ask at 100 and trades at 100, not 105.
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    auto trades = book.add_order(make(2, Side::Buy, 105, 5));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, 100);
}

// ============================================================
// Multi-level sweeps and the limit price boundary
// ============================================================

TEST(OrderBook, BuySweepsMultipleAskLevels) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    book.add_order(make(2, Side::Sell, 101, 5));
    auto trades = book.add_order(make(3, Side::Buy, 101, 8));

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(trades[1].price, 101);
    EXPECT_EQ(trades[1].quantity, 3);
    EXPECT_EQ(book.volume_at(Side::Sell, 101), 2); // 2 left at 101
}

TEST(OrderBook, GtcStopsAtLimitPriceAndRestsRemainder) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    book.add_order(make(2, Side::Sell, 102, 5));  // above the buy's limit
    auto trades = book.add_order(make(3, Side::Buy, 101, 8));

    ASSERT_EQ(trades.size(), 1u);                  // only the 100 level fills
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(book.best_bid(), 101);               // remaining 3 rested
    EXPECT_EQ(book.volume_at(Side::Buy, 101), 3);
    EXPECT_EQ(book.best_ask(), 102);               // 102 untouched
    EXPECT_EQ(book.volume_at(Side::Sell, 102), 5);
}

// ============================================================
// Market orders (ignore price, never rest a remainder)
// ============================================================

TEST(OrderBook, MarketBuyWalksBookIgnoringPrice) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    book.add_order(make(2, Side::Sell, 101, 5));
    auto trades = book.add_order(make(3, Side::Buy, 0, 8, OrderType::Market));

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[1].price, 101);
    EXPECT_EQ(book.volume_at(Side::Sell, 101), 2);
}

TEST(OrderBook, MarketOrderDiscardsUnfillableRemainder) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 10));
    auto trades = book.add_order(make(2, Side::Buy, 0, 15, OrderType::Market));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 10);           // took all available
    EXPECT_FALSE(book.best_ask().has_value());   // book emptied
    EXPECT_FALSE(book.best_bid().has_value());   // remainder NOT rested
}

// ============================================================
// Fill-or-Kill
// ============================================================

TEST(OrderBook, FillOrKillKilledWhenInsufficientLiquidity) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 3));  // only 3 available
    auto trades = book.add_order(make(2, Side::Buy, 100, 10, OrderType::FillOrKill));

    EXPECT_TRUE(trades.empty());                  // all-or-nothing -> killed
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 3);// resting sell untouched
}

TEST(OrderBook, FillOrKillKilledWhenLiquidityBeyondLimitPrice) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 3));
    book.add_order(make(2, Side::Sell, 105, 10)); // enough qty, but above limit
    auto trades = book.add_order(make(3, Side::Buy, 101, 8, OrderType::FillOrKill));

    EXPECT_TRUE(trades.empty());                  // only 3 reachable within price -> killed
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 3);
    EXPECT_EQ(book.volume_at(Side::Sell, 105), 10);
}

TEST(OrderBook, FillOrKillExecutesWhenFullyFillable) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 6));
    book.add_order(make(2, Side::Sell, 101, 6));
    auto trades = book.add_order(make(3, Side::Buy, 101, 10, OrderType::FillOrKill));

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].quantity, 6);
    EXPECT_EQ(trades[1].quantity, 4);
    EXPECT_EQ(book.volume_at(Side::Sell, 101), 2);
    EXPECT_FALSE(book.best_bid().has_value());    // FOK never rests
}

// ============================================================
// Immediate-or-Cancel
// ============================================================

TEST(OrderBook, ImmediateOrCancelFillsThenDiscardsRemainder) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 4));
    auto trades = book.add_order(make(2, Side::Buy, 100, 10, OrderType::ImmediateOrCancel));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 4);            // took what was there
    EXPECT_FALSE(book.best_bid().has_value());   // remainder NOT rested
    EXPECT_FALSE(book.best_ask().has_value());   // resting ask consumed
}

TEST(OrderBook, ImmediateOrCancelStopsAtLimitPrice) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    book.add_order(make(2, Side::Sell, 103, 5)); // above limit
    auto trades = book.add_order(make(3, Side::Buy, 101, 10, OrderType::ImmediateOrCancel));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(book.volume_at(Side::Sell, 103), 5); // untouched
    EXPECT_FALSE(book.best_bid().has_value());      // remainder discarded
}

TEST(OrderBook, ImmediateOrCancelWithNoLiquidityProducesNothing) {
    OrderBook book;
    auto trades = book.add_order(make(1, Side::Buy, 100, 10, OrderType::ImmediateOrCancel));

    EXPECT_TRUE(trades.empty());
    EXPECT_FALSE(book.best_bid().has_value());
}

// ============================================================
// Cancellation
// ============================================================

TEST(OrderBook, CancelRemovesRestingOrder) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 100, 10));
    ASSERT_EQ(book.best_bid(), 100);

    book.cancel_order(1);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 0);
}

TEST(OrderBook, CancelOneOrderLeavesOthersAtSameLevel) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 100, 5));  // first in
    book.add_order(make(2, Side::Buy, 100, 5));  // second in

    book.cancel_order(1);
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 5); // only order 2 remains

    // The surviving order (id 2) should now be the one that trades.
    auto trades = book.add_order(make(3, Side::Sell, 100, 5));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_id, 2);
}

TEST(OrderBook, CancelUnknownIdIsNoOp) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 100, 10));
    book.cancel_order(999);
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 10);
}

TEST(OrderBook, CancelledOrderNoLongerMatches) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    book.cancel_order(1);

    auto trades = book.add_order(make(2, Side::Buy, 100, 5));
    EXPECT_TRUE(trades.empty());                  // nothing to match
    EXPECT_EQ(book.best_bid(), 100);              // buy rests instead
}

TEST(OrderBook, RestingQuantity) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 10));
    book.add_order(make(2, Side::Sell, 99, 9));
    book.add_order(make(3, Side::Buy, 98, 8));
    book.add_order(make(4, Side::Buy, 97, 7));

    EXPECT_EQ(book.resting_quantity(), 34);
    EXPECT_EQ(book.resting_order_count(), 4);
}

TEST(OrderBook, RestingQuantityEmptyBookIsZero) {
    OrderBook book;
    EXPECT_EQ(book.resting_quantity(), 0);
    EXPECT_EQ(book.resting_order_count(), 0);
}

TEST(OrderBook, RestingQuantityReflectsRemainingAfterFill) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 10));   // rests: 10
    book.add_order(make(2, Side::Buy,  100, 4));     // takes 4 -> maker has 6 left

    // if the impl wrongly summed initial_quantity this would be 10
    EXPECT_EQ(book.resting_quantity(), 6);
    EXPECT_EQ(book.resting_order_count(), 1);
}

TEST(OrderBook, RestingQuantitySumsWithinAPriceLevel) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 50, 5));
    book.add_order(make(2, Side::Buy, 50, 7));   // same price, two orders
    EXPECT_EQ(book.resting_quantity(), 12);
    EXPECT_EQ(book.resting_order_count(), 2);
}

TEST(OrderBook, RestingQuantityDropsAfterCancel) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 50, 5));
    book.add_order(make(2, Side::Buy, 49, 7));
    EXPECT_EQ(book.resting_quantity(), 12);
    EXPECT_EQ(book.resting_order_count(), 2);

    book.cancel_order(1);
    EXPECT_EQ(book.resting_quantity(), 7);       // order 1's 5 removed
    EXPECT_EQ(book.resting_order_count(), 1);
}

// ============================================================
// audit() — the book stays internally consistent through the public API.
// (These are all positive checks: reached via add/cancel, audit must stay true.)
// ============================================================

TEST(OrderBook, AuditTrueOnEmptyBook) {
    OrderBook book;
    EXPECT_TRUE(book.audit());
}

TEST(OrderBook, AuditTrueWithRestingBothSides) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 101, 5));
    book.add_order(make(2, Side::Sell, 102, 5));
    book.add_order(make(3, Side::Buy, 99, 5));
    book.add_order(make(4, Side::Buy, 98, 5));
    EXPECT_TRUE(book.audit());
}

TEST(OrderBook, AuditTrueAfterPartialFill) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 10));
    book.add_order(make(2, Side::Buy, 100, 4));   // maker left with 6 resting
    EXPECT_TRUE(book.audit());
}

TEST(OrderBook, AuditTrueAfterFullFillEmptiesBook) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    book.add_order(make(2, Side::Buy, 100, 5));   // exact fill -> book empty
    EXPECT_TRUE(book.audit());
}

TEST(OrderBook, AuditTrueAfterMultiLevelSweep) {
    OrderBook book;
    book.add_order(make(1, Side::Sell, 100, 5));
    book.add_order(make(2, Side::Sell, 101, 5));
    book.add_order(make(3, Side::Buy, 101, 8));   // sweeps 100, partially takes 101
    EXPECT_TRUE(book.audit());
}

TEST(OrderBook, AuditTrueAfterCancels) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 50, 5));
    book.add_order(make(2, Side::Buy, 50, 7));   // two at one level
    book.add_order(make(3, Side::Sell, 60, 3));
    book.cancel_order(1);                         // remove one of a level's two
    book.cancel_order(3);                         // remove a whole level
    book.cancel_order(999);                       // unknown id -> no-op
    EXPECT_TRUE(book.audit());
}

TEST(OrderBook, AuditTrueWithMultipleOrdersPerLevel) {
    OrderBook book;
    book.add_order(make(1, Side::Buy, 50, 5));
    book.add_order(make(2, Side::Buy, 50, 5));
    book.add_order(make(3, Side::Buy, 50, 5));   // FIFO list of three at 50
    book.add_order(make(4, Side::Sell, 51, 5));
    EXPECT_TRUE(book.audit());
}