#pragma once

#include <map>
#include <vector>

#include "Order.hpp"

namespace poly
{
    class OrderBook {
        public:
            std::vector<Trade> add_order(const Order& order);
        
        private:
            std::map<Price, Quantity, std::greater<Price>> bids;

            std::map<Price, Quantity, std::less<Price>> asks;

            std::vector<Trade> match(Order order);
    };
}
