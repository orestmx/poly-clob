#include "engine/OrderBook.hpp"
#include <iostream>
#include <vector>
#include <string>

using namespace clob;

// ---- tiny test harness -------------------------------------------------
static int g_failures = 0;

static void check(bool cond, const std::string& name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

// Build an order; remaining_quantity starts equal to initial quantity.
static Order make(OrderId id, Side side, Price price, Quantity qty, OrderType type) {
    return Order{ id, side, price, qty, qty, type };
}

// Total quantity across a set of trades.
static Quantity total_qty(const std::vector<Trade>& trades) {
    Quantity sum = 0;
    for (const auto& t : trades) sum += t.quantity;
    return sum;
}

// ---- tests -------------------------------------------------------------

// Resting limit orders that don't cross should produce no trades.
static void test_resting_no_match() {
    std::cout << "test_resting_no_match\n";
    OrderBook book;
    auto t1 = book.add_order(make(1, Side::Sell, 55, 10, OrderType::GoodTillCancel));
    auto t2 = book.add_order(make(2, Side::Buy,  50, 10, OrderType::GoodTillCancel)); // 50 < 55, no cross
    check(t1.empty(), "resting sell -> 0 trades");
    check(t2.empty(), "non-crossing buy -> 0 trades");
}

// A buy that exactly matches one resting sell fills fully in one trade.
static void test_full_fill() {
    std::cout << "test_full_fill\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 10, OrderType::GoodTillCancel));
    auto trades = book.add_order(make(2, Side::Buy, 50, 10, OrderType::GoodTillCancel));
    check(trades.size() == 1,        "one trade produced");
    check(total_qty(trades) == 10,   "10 units filled");
    check(!trades.empty() && trades[0].price == 50, "filled at maker price 50");
    check(!trades.empty() && trades[0].maker_id == 1 && trades[0].taker_id == 2,
          "maker=1, taker=2");
}

// A taker larger than the top level walks into deeper levels (partial fill).
static void test_partial_fill_sweeps_levels() {
    std::cout << "test_partial_fill_sweeps_levels\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 5, OrderType::GoodTillCancel));
    auto trades = book.add_order(make(3, Side::Buy, 51, 8, OrderType::GoodTillCancel));
    check(trades.size() == 2,      "two trades (one per level)");
    check(total_qty(trades) == 8,  "8 units filled total");
    check(trades.size() == 2 && trades[0].price == 50 && trades[0].quantity == 5,
          "first 5 @ 50");
    check(trades.size() == 2 && trades[1].price == 51 && trades[1].quantity == 3,
          "then 3 @ 51");
    // 2 units of order #2 remain resting @ 51; a market buy should hit them.
    auto rest = book.add_order(make(4, Side::Buy, 0, 10, OrderType::Market));
    check(total_qty(rest) == 2,    "2 units of remainder still resting @ 51");
}

// When the maker is bigger than the taker, the maker stays on the book.
static void test_maker_remainder_stays() {
    std::cout << "test_maker_remainder_stays\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 10, OrderType::GoodTillCancel));
    auto t1 = book.add_order(make(2, Side::Buy, 50, 4, OrderType::GoodTillCancel));
    check(total_qty(t1) == 4, "first taker fills 4");
    auto t2 = book.add_order(make(3, Side::Buy, 50, 6, OrderType::GoodTillCancel));
    check(total_qty(t2) == 6, "remaining 6 still available");
    auto t3 = book.add_order(make(4, Side::Buy, 50, 1, OrderType::GoodTillCancel));
    check(t3.empty(), "book now empty -> 0 trades");
}

// Two orders at the same price fill in arrival order (price-time priority).
static void test_fifo_time_priority() {
    std::cout << "test_fifo_time_priority\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel)); // earlier
    book.add_order(make(2, Side::Sell, 50, 5, OrderType::GoodTillCancel)); // later
    auto trades = book.add_order(make(3, Side::Buy, 50, 5, OrderType::GoodTillCancel));
    check(trades.size() == 1 && trades[0].maker_id == 1,
          "earlier order (id=1) fills first");
}

// A market order against an empty book produces nothing.
static void test_market_no_liquidity() {
    std::cout << "test_market_no_liquidity\n";
    OrderBook book;
    auto trades = book.add_order(make(1, Side::Buy, 0, 10, OrderType::Market));
    check(trades.empty(), "market buy on empty book -> 0 trades");
}

// A market order sweeps multiple levels until filled.
static void test_market_sweeps() {
    std::cout << "test_market_sweeps\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 3, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 3, OrderType::GoodTillCancel));
    book.add_order(make(3, Side::Sell, 52, 3, OrderType::GoodTillCancel));
    auto trades = book.add_order(make(4, Side::Buy, 0, 7, OrderType::Market));
    check(trades.size() == 3,     "three trades across three levels");
    check(total_qty(trades) == 7, "7 units filled");
}

// A sell taker matches against the bids side (highest bid first).
static void test_sell_hits_bids() {
    std::cout << "test_sell_hits_bids\n";
    OrderBook book;
    book.add_order(make(1, Side::Buy, 49, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Buy, 50, 5, OrderType::GoodTillCancel)); // best bid
    auto trades = book.add_order(make(3, Side::Sell, 49, 5, OrderType::GoodTillCancel));
    check(trades.size() == 1 && trades[0].price == 50,
          "sell hits highest bid (50) first");
    check(trades.size() == 1 && trades[0].maker_id == 2, "maker is the 50 bid (id=2)");
}

int main() {
    test_resting_no_match();
    test_full_fill();
    test_partial_fill_sweeps_levels();
    test_maker_remainder_stays();
    test_fifo_time_priority();
    test_market_no_liquidity();
    test_market_sweeps();
    test_sell_hits_bids();

    std::cout << "\n"
              << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
