#pragma once
#include <vector>
#include "Types.hpp"

namespace poly {
    struct Order
    {
        OrderId id;
        Side side;
        Price price;
        Quantity quantity;
    };

    struct Trade
    {
        OrderId maker_id;
        OrderId taker_id;
        Price price;
        Quantity quantity;
    };
}