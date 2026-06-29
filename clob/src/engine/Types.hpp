#pragma once
#include <cstdint>
#include <list>
#include <memory>

namespace clob {
    using Price = std::int32_t;
    using Quantity = std::int32_t;
    using OrderId = std::int64_t;

    struct Order;

    using OrderPointer = std::shared_ptr<Order>;
    using OrderPointers = std::list<OrderPointer>;

    enum class Side {
        Buy,
        Sell
    };

    enum class OrderType {
        GoodTillCancel, // Active until completly filled
        FillOrKill, // Fill in full or kill immideately
        ImmediateOrCancel, // Fills as much as possible and cancels unfilled portion
        Market // Fills at market rate
    };
}