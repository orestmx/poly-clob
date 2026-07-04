#include "OrderFlowGenerator.hpp"
#include "engine/Types.hpp"
#include <random>

namespace clob::testing {
    OrderFlowGenerator::OrderFlowGenerator(std::uint64_t seed, Config cfg)
        : rng_(seed), cfg_(cfg) {}

    Order OrderFlowGenerator::make(OrderId id, Side side, Price price, Quantity qty,
                  OrderType type) {
        return Order{id, side, price, qty, qty, type};
    }

    Action OrderFlowGenerator::next() {
        std::uniform_real_distribution<double> dist_cancel(0, 1);

        if (!issued_.empty() && dist_cancel(rng_) < cfg_.p_cancel) {
            std::uniform_int_distribution<std::size_t> idx_dist(0, issued_.size() - 1);
            OrderId cancel_id = issued_[idx_dist(rng_)];
            return Action{ActionKind::Cancel, Order{}, cancel_id};
        }

        OrderId id = next_id_++;
        Order order = make(id, random_side(), random_price(), random_qty(), random_type());
        issued_.push_back(id);
        return Action{ActionKind::Add, order, OrderId{}};
    }

    Side OrderFlowGenerator::random_side() {
        std::bernoulli_distribution dist(0.5);
        return dist(rng_) ? Side::Buy : Side::Sell;
    }

    Price OrderFlowGenerator::random_price() {
        std::uniform_int_distribution<Price> dist(cfg_.min_price, cfg_.max_price);
        return dist(rng_);
    }

    Quantity OrderFlowGenerator::random_qty() {
        std::uniform_int_distribution<Quantity> dist(cfg_.min_qty, cfg_.max_qty);
        return dist(rng_);
    }

    OrderType OrderFlowGenerator::random_type() {
        std::discrete_distribution<int> dist({
            static_cast<double>(cfg_.w_gtc),
            static_cast<double>(cfg_.w_mkt),
            static_cast<double>(cfg_.w_fok),
            static_cast<double>(cfg_.w_ioc)
        });
        switch (dist(rng_)) {
            case 0: return OrderType::GoodTillCancel;
            case 1: return OrderType::Market;
            case 2: return OrderType::FillOrKill;
            default: return OrderType::ImmediateOrCancel;
        }
    }
}