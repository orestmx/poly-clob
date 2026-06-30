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
            template <typename BookSide>
            std::vector<Trade> match_side(OrderPointer taker, BookSide& book);

            std::vector<Trade> add_order(Order order);

            template <typename BookSide>
            void rest_order(OrderPointer order, BookSide& book);

            void cancel_order(OrderId id);

        private:
            // helper for the Id lookup map
            struct OrderEntry {
                OrderPointer pointer;
                OrderPointers::iterator position; // points to the order's spot in the FIFO list
            };

            // Bids: Price -> List of Orders (Highest price first)
            std::map<Price, OrderPointers, std::greater<Price>> bids;

            // Asks: Price -> List of Orders (Lowest price first)
            std::map<Price, OrderPointers, std::less<Price>> asks;

            // Instant lookup
            std::unordered_map<OrderId, OrderEntry> id_map;

            // Logic dispachers
            std::vector<Trade> execute_gtc_order(OrderPointer order);
            std::vector<Trade> execute_market_order(OrderPointer order);
            std::vector<Trade> match_against_book(OrderPointer order);
    };
}
