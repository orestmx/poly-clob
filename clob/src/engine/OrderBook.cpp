#include "OrderBook.hpp"
#include <algorithm>
#include <iostream>

namespace poly {

    std::vector<Trade> OrderBook::add_order(const Order& order) {
        if (order.side == Side::Buy) {
            bids[order.price] += order.quantity;
        } else {
            asks[order.price] += order.quantity;
        }

        return {};
    }

    std::vector<Trade> OrderBook::match(Order order) {
        return {};
    }
}