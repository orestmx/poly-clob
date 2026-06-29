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

    std::vector<Trade> OrderBook::match_against_book(OrderPointer taker_order) {
        std::vector<Trade> trades;

        auto& target_book = (taker_order->side == Side::Buy) ? asks : bids;

        auto it = asks.begin();
        while (it != asks.end() && taker_order->remaining_quantity > 0) {
            Price best_ask_price = it->first;
            
            if (taker_order->type != OrderType::Market && it->first > taker_order->price) break;
            
            auto& order_list = it->second;
            while (!order_list.empty() && taker_order->remaining_quantity > 0)
            {
                OrderPointer maker_order = order_list.front();

                Quantity fill_qty = std::min(taker_order->remaining_quantity, maker_order->remaining_quantity);
                
                trades.push_back({
                    maker_order->id,
                    taker_order->id,
                    best_ask_price,
                    fill_qty,
                    taker_order->type,
                    0 // timestamp place holder
                });

                taker_order->fill(fill_qty);
                maker_order->fill(fill_qty);
                
                if (maker_order->is_filled()) {
                    id_map.erase(maker_order->id);
                    order_list.pop_front();
                }
            }
                

            }
            

        } else {
            
        }

        return {};
    }
}