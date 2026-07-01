#include "OrderBook.hpp"
#include "engine/Order.hpp"
#include "engine/Types.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <vector>

namespace clob {

    std::vector<Trade> OrderBook::add_order(Order order) {

        auto order_ptr = std::make_shared<Order>(std::move(order));

        switch (order_ptr->type)
        {
        case OrderType::GoodTillCancel:
            return execute_gtc_order(order_ptr);

        case OrderType::Market:
            return execute_market_order(order_ptr);

        case OrderType::FillOrKill:
            return execute_fok_order(order_ptr);

        case OrderType::ImmediateOrCancel:
            return execute_ioc_order(order_ptr);

        default:
            return{};
        }
    }

    // ---------------- Order Execution ----------------
    std::vector<Trade> OrderBook::execute_market_order(OrderPointer order) {
        return match_against_book(order);
    }

    std::vector<Trade> OrderBook::execute_gtc_order(OrderPointer order) {
        auto trades = match_against_book(order);

        if (order->remaining_quantity > 0) {
            if (order->side == Side::Buy) rest_order(order, bids);
            else                          rest_order(order, asks);
        }

        return trades;
    }

    std::vector<Trade> OrderBook::execute_fok_order(OrderPointer order) {
        if (!can_fully_fill(order)) return{};
        return match_against_book(order);
    }

    std::vector<Trade> OrderBook::execute_ioc_order(OrderPointer order) {
        return match_against_book(order);
    }

    // ---------------- Order Cancalation ----------------
    void OrderBook::cancel_order(OrderId id) {
        auto it = id_map.find(id);
        if (it == id_map.end()) return;

        const OrderEntry& entry = it->second;
        if (entry.pointer->side == Side::Buy) remove_order(entry, bids);
        else                                  remove_order(entry, asks);

        id_map.erase(it);
    }

    template <typename BookSide>
    void OrderBook::remove_order(const OrderEntry& entry, BookSide& book) {
        const Price price = entry.pointer->price;

        auto level = book.find(price);
        if (level == book.end()) return;

        auto& order_list = level->second;
        order_list.erase(entry.position);

        if (order_list.empty()) book.erase(level);
    }

    // ---------------- Rest order ----------------
    template <typename BookSide>
    void OrderBook::rest_order(OrderPointer order, BookSide& book) {
        auto& level = book[order->price];
        level.push_back(order);

        id_map[order->id] = OrderEntry{order, std::prev(level.end())};
    }

    // ---------------- Liquidity check ----------------
    bool OrderBook::can_fully_fill(const OrderPointer& order) const {
        return (order->side == Side::Buy) ? has_liquidity(order, asks)
                                          : has_liquidity(order, bids);
    }

    template <typename BookSide>
    bool OrderBook::has_liquidity(const OrderPointer& order, const BookSide& book) const {
        Quantity needed = order->remaining_quantity;

        for (const auto& [price, level] : book) {
            const bool crossed = (order->side == Side::Buy) ? (order->price < price)
                                                            : (order->price > price);
            if (crossed || needed <= 0) break;

            Quantity level_qty = 0;
            for (const auto& o : level) level_qty += o->remaining_quantity;

            needed -= level_qty;
        }

        return (needed <= 0);
    }

    // ---------------- Book matching ----------------
    std::vector<Trade> OrderBook::match_against_book(OrderPointer order) {
        return (order->side == Side::Buy) ? match_side(order, asks)
                                          : match_side(order, bids);
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

    // ---------------- Book visualisations ----------------
    std::optional<Price> OrderBook::best_bid() const {
        if (bids.empty()) return std::nullopt;
        return bids.begin()->first;
    }

    std::optional<Price> OrderBook::best_ask() const {
        if (asks.empty()) return std::nullopt;
        return asks.begin()->first;
    }

    void OrderBook::print_book() const {
        std::cout << "----- ASKS (low -> high) -----\n";
        // print asks high-to-low so the spread sits in the middle, like a real ladder
        for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
            Quantity level_qty = 0;
            for (const auto& o : it->second) level_qty += o->remaining_quantity;
            std::cout << "  " << it->first << " x " << level_qty << "\n";
        }
        std::cout << "----- BIDS (high -> low) -----\n";
        for (const auto& [price, list] : bids) {
            Quantity level_qty = 0;
            for (const auto& o : list) level_qty += o->remaining_quantity;
            std::cout << "  " << price << " x " << level_qty << "\n";
        }
    }
}