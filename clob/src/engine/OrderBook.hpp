#pragma once

#include <map>
#include <unordered_map>
#include <vector>
#include <vector>

#include "Order.hpp"

namespace clob
{
    class OrderBook {
        public:
            std::vector<Trade> add_order(Order order);

            void cancel_order(OrderId id);

        private:
            // helper for the Id lookup map
            struct OrderEntry {
                OrderPointer pointer;
                OrderPointers::iterator position; // points to the order's spot in the FIFO list
            };

            // Bids: Price -> List of Orders (Highest price first)
            // std::map<Price, OrderPointers, std::greater<Price>> bids;
            std::map<Price, OrderPointers> bids; // for fast compile, to change later

            // Asks: Price -> List of Orders (Lowest price first)
            // std::map<Price, OrderPointers, std::less<Price>> asks;
            std::map<Price, OrderPointers> asks; // for fast compile, to change later

            // Instant lookup
            std::unordered_map<OrderId, OrderEntry> id_map;

            // Logic dispachers
            std::vector<Trade> execute_gtc_order(OrderPointer order);
            std::vector<Trade> execute_market_order(OrderPointer order);
            std::vector<Trade> match_against_book(OrderPointer order);
    };
}
