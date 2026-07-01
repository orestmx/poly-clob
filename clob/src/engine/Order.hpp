#pragma once
#include "Types.hpp"

namespace clob {
    struct Order {
        OrderId id;
        Side side;
        Price price;
        Quantity initial_quantity; // initial quantity
        Quantity remaining_quantity; // remaining quantity
        OrderType type;

        bool is_filled() const {return remaining_quantity <= 0;}
        void fill(Quantity qty) {remaining_quantity -= qty;}
    };

    struct Trade
    {
        OrderId maker_id;
        OrderId taker_id;
        Price price;
        Quantity quantity;
        Side side;
        uint64_t timestamp;
    };
}