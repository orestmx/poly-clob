#pragma once

#include <map>
#include <unordered_map>
#include <vector>
#include <optional>

#include "Order.hpp"

namespace clob
{
    class OrderBook {
        public:
            std::vector<Trade> add_order(Order order);
            void cancel_order(OrderId id);

            std::optional<Price> best_bid() const;
            std::optional<Price> best_ask() const;
            void print_book() const;

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
            std::vector<Trade> execute_fok_order(OrderPointer order);
            std::vector<Trade> execute_ioc_order(OrderPointer order);
            std::vector<Trade> match_against_book(OrderPointer order);

            template <typename BookSide>
            std::vector<Trade> match_side(OrderPointer taker, BookSide& book);

            template <typename BookSide>
            void rest_order(OrderPointer order, BookSide& book);

            template <typename BookSide>
            void remove_order(const OrderEntry& entry, BookSide& book);

            template <typename BookSide>
            bool has_liquidity(const OrderPointer& order, const BookSide& book) const;

            bool can_fully_fill(const OrderPointer& order) const;
    };
}
