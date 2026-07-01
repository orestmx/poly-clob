#include "engine/OrderBook.hpp"
#include <iostream>
#include <optional>
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

// ---- matching tests ----------------------------------------------------

static void test_resting_no_match() {
    std::cout << "test_resting_no_match\n";
    OrderBook book;
    auto t1 = book.add_order(make(1, Side::Sell, 55, 10, OrderType::GoodTillCancel));
    auto t2 = book.add_order(make(2, Side::Buy,  50, 10, OrderType::GoodTillCancel));
    check(t1.empty(), "resting sell -> 0 trades");
    check(t2.empty(), "non-crossing buy -> 0 trades");
}

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

static void test_partial_fill_sweeps_levels() {
    std::cout << "test_partial_fill_sweeps_levels\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 5, OrderType::GoodTillCancel));
    auto trades = book.add_order(make(3, Side::Buy, 51, 8, OrderType::GoodTillCancel));
    check(trades.size() == 2,      "two trades (one per level)");
    check(total_qty(trades) == 8,  "8 units filled total");
    check(trades.size() == 2 && trades[0].price == 50 && trades[0].quantity == 5, "first 5 @ 50");
    check(trades.size() == 2 && trades[1].price == 51 && trades[1].quantity == 3, "then 3 @ 51");
    auto rest = book.add_order(make(4, Side::Buy, 0, 10, OrderType::Market));
    check(total_qty(rest) == 2,    "2 units of remainder still resting @ 51");
}

static void test_fifo_time_priority() {
    std::cout << "test_fifo_time_priority\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel)); // earlier
    book.add_order(make(2, Side::Sell, 50, 5, OrderType::GoodTillCancel)); // later
    auto trades = book.add_order(make(3, Side::Buy, 50, 5, OrderType::GoodTillCancel));
    check(trades.size() == 1 && trades[0].maker_id == 1, "earlier order (id=1) fills first");
}

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

// ---- inspector tests (best_bid / best_ask) -----------------------------

static void test_inspectors() {
    std::cout << "test_inspectors\n";
    OrderBook book;
    check(book.best_bid() == std::nullopt, "empty book -> no best bid");
    check(book.best_ask() == std::nullopt, "empty book -> no best ask");

    book.add_order(make(1, Side::Sell, 55, 10, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 56, 10, OrderType::GoodTillCancel));
    book.add_order(make(3, Side::Buy,  50, 10, OrderType::GoodTillCancel));
    book.add_order(make(4, Side::Buy,  49, 10, OrderType::GoodTillCancel));
    check(book.best_ask() == 55, "best ask is the lowest ask (55)");
    check(book.best_bid() == 50, "best bid is the highest bid (50)");
}

// ---- cancel tests ------------------------------------------------------

static void test_cancel_removes_liquidity() {
    std::cout << "test_cancel_removes_liquidity\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    check(book.best_ask() == 50, "ask resting at 50");
    book.cancel_order(1);
    check(book.best_ask() == std::nullopt, "ask gone after cancel");
    auto trades = book.add_order(make(2, Side::Buy, 50, 5, OrderType::GoodTillCancel));
    check(trades.empty(), "buy finds nothing to match -> 0 trades");
}

static void test_cancel_keeps_other_levels() {
    std::cout << "test_cancel_keeps_other_levels\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 5, OrderType::GoodTillCancel));
    book.cancel_order(1);                       // cancel best level only
    check(book.best_ask() == 51, "best ask moves up to 51");
    auto trades = book.add_order(make(3, Side::Buy, 51, 5, OrderType::GoodTillCancel));
    check(trades.size() == 1 && trades[0].maker_id == 2, "remaining level (id=2) still matches");
}

static void test_cancel_partially_filled() {
    std::cout << "test_cancel_partially_filled\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 10, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Buy,  50, 4,  OrderType::GoodTillCancel)); // fills 4, 6 rest
    check(book.best_ask() == 50, "6 units of maker still resting @ 50");
    book.cancel_order(1);                       // cancel the partially-filled maker
    check(book.best_ask() == std::nullopt, "remainder removed by cancel");
    auto trades = book.add_order(make(3, Side::Buy, 50, 6, OrderType::GoodTillCancel));
    check(trades.empty(), "nothing left to fill");
}

static void test_cancel_unknown_id_is_safe() {
    std::cout << "test_cancel_unknown_id_is_safe\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.cancel_order(999);                      // never existed
    check(book.best_ask() == 50, "unknown-id cancel is a no-op");
    auto trades = book.add_order(make(2, Side::Buy, 50, 5, OrderType::GoodTillCancel));
    check(total_qty(trades) == 5, "untouched order still matches");
}

static void test_cancel_bid_side() {
    std::cout << "test_cancel_bid_side\n";
    OrderBook book;
    book.add_order(make(1, Side::Buy, 50, 5, OrderType::GoodTillCancel));
    check(book.best_bid() == 50, "bid resting at 50");
    book.cancel_order(1);
    check(book.best_bid() == std::nullopt, "bid removed (cancel works on bids too)");
}

// ---- fill-or-kill tests ------------------------------------------------

static void test_fok_full_fill() {
    std::cout << "test_fok_full_fill\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 3, OrderType::GoodTillCancel)); // 8 available <= 51
    auto trades = book.add_order(make(3, Side::Buy, 51, 8, OrderType::FillOrKill));
    check(trades.size() == 2,      "FOK fills fully across two levels");
    check(total_qty(trades) == 8,  "8 units filled");
    check(book.best_ask() == std::nullopt, "book fully consumed, nothing rests");
}

static void test_fok_kill_insufficient_qty() {
    std::cout << "test_fok_kill_insufficient_qty\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 3, OrderType::GoodTillCancel)); // only 8 available
    auto trades = book.add_order(make(3, Side::Buy, 51, 9, OrderType::FillOrKill)); // needs 9
    check(trades.empty(),        "FOK killed (not enough liquidity) -> 0 trades");
    check(book.best_ask() == 50, "book untouched: best ask still 50");
    // Prove nothing was consumed: the full 8 is still matchable afterwards.
    auto after = book.add_order(make(4, Side::Buy, 51, 8, OrderType::GoodTillCancel));
    check(total_qty(after) == 8, "all 8 units still available after killed FOK");
}

static void test_fok_respects_limit_price() {
    std::cout << "test_fok_respects_limit_price\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5,  OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 55, 10, OrderType::GoodTillCancel)); // above the limit
    // Need 8, but only 5 sits at a price <= 51; the 55 level must not count.
    auto trades = book.add_order(make(3, Side::Buy, 51, 8, OrderType::FillOrKill));
    check(trades.empty(),        "FOK killed: liquidity above limit price doesn't count");
    check(book.best_ask() == 50, "book unchanged");
}

static void test_fok_sell_side() {
    std::cout << "test_fok_sell_side\n";
    OrderBook book;
    book.add_order(make(1, Side::Buy, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Buy, 49, 5, OrderType::GoodTillCancel));
    auto trades = book.add_order(make(3, Side::Sell, 49, 8, OrderType::FillOrKill));
    check(trades.size() == 2,      "FOK sell fills across bids");
    check(total_qty(trades) == 8,  "8 units filled");
    check(!trades.empty() && trades[0].price == 50, "highest bid (50) hit first");
}

// ---- immediate-or-cancel tests -----------------------------------------

static void test_ioc_partial_then_discard() {
    std::cout << "test_ioc_partial_then_discard\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    auto trades = book.add_order(make(2, Side::Buy, 50, 8, OrderType::ImmediateOrCancel));
    check(total_qty(trades) == 5, "IOC fills the 5 available");
    check(book.best_ask() == std::nullopt, "resting sell fully consumed");
    // The distinguishing property vs GTC: the unfilled 3 is NOT rested.
    check(book.best_bid() == std::nullopt, "IOC remainder discarded, not rested");
}

static void test_ioc_respects_limit_price() {
    std::cout << "test_ioc_respects_limit_price\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 55, 5, OrderType::GoodTillCancel)); // above limit
    auto trades = book.add_order(make(3, Side::Buy, 51, 8, OrderType::ImmediateOrCancel));
    check(total_qty(trades) == 5, "IOC fills only what's <= its limit price");
    check(book.best_ask() == 55, "the 55 level is left untouched");
    check(book.best_bid() == std::nullopt, "no IOC remainder rested");
}

static void test_ioc_full_fill() {
    std::cout << "test_ioc_full_fill\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 50, 5, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 5, OrderType::GoodTillCancel));
    auto trades = book.add_order(make(3, Side::Buy, 51, 8, OrderType::ImmediateOrCancel));
    check(trades.size() == 2,     "IOC fills across two levels");
    check(total_qty(trades) == 8, "8 units filled");
    check(book.best_ask() == 51,  "2 units remain resting @ 51 (maker side)");
}

static void test_ioc_no_liquidity() {
    std::cout << "test_ioc_no_liquidity\n";
    OrderBook book;
    auto trades = book.add_order(make(1, Side::Buy, 50, 5, OrderType::ImmediateOrCancel));
    check(trades.empty(),                   "IOC on empty book -> 0 trades");
    check(book.best_bid() == std::nullopt,  "nothing rested");
}

// ---- visual demo using print_book --------------------------------------

static void demo_print_book() {
    std::cout << "\n========== print_book demo ==========\n";
    OrderBook book;
    book.add_order(make(1, Side::Sell, 52, 4, OrderType::GoodTillCancel));
    book.add_order(make(2, Side::Sell, 51, 3, OrderType::GoodTillCancel));
    book.add_order(make(3, Side::Sell, 50, 2, OrderType::GoodTillCancel));
    book.add_order(make(4, Side::Buy,  48, 5, OrderType::GoodTillCancel));
    book.add_order(make(5, Side::Buy,  47, 6, OrderType::GoodTillCancel));

    std::cout << "\n[1] Initial book:\n";
    book.print_book();

    std::cout << "\n[2] After a market BUY of 4 (sweeps asks from the top):\n";
    book.add_order(make(6, Side::Buy, 0, 4, OrderType::Market));
    book.print_book();

    std::cout << "\n[3] After cancelling order id=4 (the 48 bid):\n";
    book.cancel_order(4);
    book.print_book();
    std::cout << "=====================================\n";
}

int main() {
    test_resting_no_match();
    test_full_fill();
    test_partial_fill_sweeps_levels();
    test_fifo_time_priority();
    test_market_sweeps();
    test_inspectors();
    test_cancel_removes_liquidity();
    test_cancel_keeps_other_levels();
    test_cancel_partially_filled();
    test_cancel_unknown_id_is_safe();
    test_cancel_bid_side();
    test_fok_full_fill();
    test_fok_kill_insufficient_qty();
    test_fok_respects_limit_price();
    test_fok_sell_side();
    test_ioc_partial_then_discard();
    test_ioc_respects_limit_price();
    test_ioc_full_fill();
    test_ioc_no_liquidity();

    std::cout << "\n"
              << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;

    demo_print_book();   // visual output (not asserted)

    return g_failures == 0 ? 0 : 1;
}
