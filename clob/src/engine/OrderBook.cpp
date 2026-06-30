#include "OrderBook.hpp"
#include <algorithm>
#include <iostream>

namespace clob {

    std::vector<Trade> OrderBook::add_order(Order order) {

        auto order_ptr = std::make_shared<Order>(std::move(order));

        switch (order_ptr->type)
        {
        case OrderType::GoodTillCancel:
            return execute_gtc_order(order_ptr);

        case OrderType::Market:
            return execute_market_order(order_ptr);

        default:
            return{};
        }
    }

    std::vector<Trade> OrderBook::execute_market_order(OrderPointer order) {
        return match_against_book(order);
    }

    std::vector<Trade> OrderBook::execute_gtc_order(OrderPointer order) {
        auto trades = match_against_book(order);

        if (order->remaining_quantity > 0) {
            if (order->side == Side::Buy) rest_order(order, bids);
            else rest_order(order, asks);
        }

        return trades;
    }

    template <typename BookSide>
    void OrderBook::rest_order(OrderPointer order, BookSide& book) {
        auto& level = book[order->price];
        level.push_back(order);

        id_map[order->id] = OrderEntry{order, std::prev(level.end())};
    }

    std::vector<Trade> OrderBook::match_against_book(OrderPointer taker_order) {
        return (taker_order->side == Side::Buy) ? match_side(taker_order, asks)
                                                : match_side(taker_order, bids);
    }

    template <typename BookSide>
    std::vector<Trade> OrderBook::match_side(OrderPointer taker_order, BookSide& target_book) {
        std::vector<Trade> trades;

        auto it = target_book.begin();
        while (it != target_book.end() && taker_order->remaining_quantity > 0) {
            const Price best_price = it->first;

            if (taker_order->type != OrderType::Market) {
                const bool crossed = (taker_order->side == Side::Buy) ? (taker_order->price < best_price)
                                                                      : (taker_order->price > best_price);

                if (crossed) break;
            }

            auto& order_list = it->second;
            while (!order_list.empty() && taker_order->remaining_quantity > 0) {
                OrderPointer maker_order = order_list.front();
                Quantity fill_qty = std::min(taker_order->remaining_quantity, maker_order->remaining_quantity);

                trades.push_back({
                    maker_order->id,
                    taker_order->id,
                    best_price,
                    fill_qty,
                    taker_order->side,
                    0 // timestamp place holder
                });

                taker_order->fill(fill_qty);
                maker_order->fill(fill_qty);

                if (maker_order->is_filled()) {
                    id_map.erase(maker_order->id);
                    order_list.pop_front();
                }
            }

            if (order_list.empty()) it = target_book.erase(it);
            else ++it;


        }

        return trades;
    }


}