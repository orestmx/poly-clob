#include "OrderBook.hpp"
#include <algorithm>

namespace poly {

    std::vector<Trade> OrderBook::add_order(const Order& order) {
        auto trades = match(order);

        return trades;
    }
}