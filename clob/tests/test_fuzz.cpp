#include "engine/Types.hpp"
#include "support/OrderFlowGenerator.hpp"
#include "engine/OrderBook.hpp"
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <unordered_map>


using namespace clob;
using namespace clob::testing;

namespace {
    // CI stays fast by default; set CLOB_FUZZ_OPS=1000000 locally for a full run
    // (e.g. after changing the engine itself).
    int fuzz_op_count() {
        if (const char* env = std::getenv("CLOB_FUZZ_OPS")) {
            return std::atoi(env);
        }
        return 200'000;
    }
}

TEST(Fuzz, InvariantsHoldUnderRandomFlow) {
    const std::uint64_t seed = 42;
    const int NUM_OPS = fuzz_op_count();

    OrderFlowGenerator gen(seed);
    OrderBook book;
    std::unordered_map<OrderId, Quantity> shadow; // tracks remaining quantities

    for (int i = 0; i < NUM_OPS; ++i) {
        Action a = gen.next();

        if (a.kind == ActionKind::Add) {
            auto pre_bid = book.best_bid();
            auto pre_ask = book.best_ask();

            auto trades = book.add_order(a.order);

            Quantity traded = 0;
            for (const auto& t : trades) {
                traded += t.quantity;

                shadow[t.maker_id] -= t.quantity;
                ASSERT_GE(shadow[t.maker_id], 0) << "overfill, seed = " << seed << "op=" << i;
                if (shadow[t.maker_id] == 0) shadow.erase(t.maker_id);

                if (a.order.side == Side::Buy && a.order.type != OrderType::Market) ASSERT_LE(t.price, a.order.price) << "execution price exceeds buy order price, seed = " << seed << "op=" << i;
                else if (a.order.side == Side::Sell && a.order.type != OrderType::Market) ASSERT_GE(t.price, a.order.price) << "execution price is smaller than sell order price, seed = " << seed << "op=" << i;

                if (a.order.side == Side::Buy) {
                    if (pre_ask) ASSERT_GE(t.price, *pre_ask) << "buy trade below pre-trade best ask, seed=" << seed << " op=" << i;
                } else {
                    if (pre_bid) ASSERT_LE(t.price, *pre_bid) << "sell trade above pre-trade best bid, seed=" << seed << " op=" << i;
                }
            }

            if (a.order.type == OrderType::GoodTillCancel && a.order.initial_quantity - traded > 0) {
            shadow[a.order.id] = a.order.initial_quantity - traded;
            }
        } else {
            book.cancel_order(a.cancel_id);
            shadow.erase(a.cancel_id);
        }

        ASSERT_TRUE(book.audit()) << "audit failed, seed=" << seed << " op=" << i;

        Quantity shadow_total = 0;
        for (const auto& [id, q] : shadow) shadow_total += q;
        ASSERT_EQ(shadow_total, book.resting_quantity()) << "conservation broke, seed=" << seed << " op=" << i;

        if (i % 100'000 == 0) {
            std::cout << "op " << i << " resting_orders=" << book.resting_order_count() << std::endl;
        }
    }
}
