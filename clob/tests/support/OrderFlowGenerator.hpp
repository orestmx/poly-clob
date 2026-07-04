#include "engine/Order.hpp"
#include "engine/Types.hpp"
#include <cstdint>
#include <random>
#include <vector>

namespace clob::testing {
    enum class ActionKind {Add, Cancel};

    struct Action {
        ActionKind kind;
        Order order;
        OrderId cancel_id;
    };

    struct GeneratorConfig {
        Price    min_price = 1, max_price = 99;
        Quantity min_qty = 1,   max_qty = 100;
        double p_cancel = 0.3; // chance of order cancel
        // relative weights for 4 order types
        int w_gtc = 70, w_mkt = 10, w_fok = 10, w_ioc = 10;
    };

    class OrderFlowGenerator {
    public:
        using Config = GeneratorConfig;

        explicit OrderFlowGenerator(std::uint64_t seed, Config cfg = {});

        Action next(); // produce next random action

    private:
        std::mt19937_64 rng_;
        Config cfg_;
        OrderId next_id_ = 1;
        std::vector<OrderId> issued_; // every id we've handed out from random cancels

        Side random_side();
        Price random_price();
        Quantity random_qty();
        OrderType random_type();

        Order make(OrderId id, Side side, Price price, Quantity qty, OrderType type);
    };
}