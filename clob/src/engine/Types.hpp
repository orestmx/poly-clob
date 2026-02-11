#pragma once
#include <cstdint>

namespace poly {
    using Price = int32_t;
    using Quantity = int32_t;
    using OrderId = int64_t;

    enum class Side {
        Buy,
        Sell
    };
}