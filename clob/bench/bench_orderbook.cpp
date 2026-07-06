#include "engine/Types.hpp"
#include "engine/OrderBook.hpp"
#include "support/OrderFlowGenerator.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace clob;
using namespace clob::testing;

namespace {
    // Tracks currently-resting order ids with O(1) random pick and O(1) removal.
    // Using unordered_map requires O(n) randompick, as it cannot be indexed randomly directly.
    // Fine for small books, but becomes a severe bottleneck for deep ones
    class LivePool {
    public:
        void add(OrderId id, Quantity qty) {
            index_[id] = ids_.size();
            ids_.push_back(id);
            qty_[id] = qty;
        }

        void decrement(OrderId id, Quantity qty) {
            auto it = qty_.find(id);
            if (it == qty_.end()) return;
            it->second -= qty;
            if (it->second <= 0) remove(id);
        }

        void remove(OrderId id) {
            auto idx_it = index_.find(id);
            if (idx_it == index_.end()) return;

            std::size_t idx = idx_it->second;
            std::size_t last = ids_.size() - 1;
            if (idx != last) {
                ids_[idx] = ids_[last];
                index_[ids_[idx]] = idx;
            }
            ids_.pop_back();
            index_.erase(id);
            qty_.erase(id);
        }

        bool empty() const { return ids_.empty(); }

        OrderId pick_random(std::mt19937_64& rng) const {
            std::uniform_int_distribution<std::size_t> dist(0, ids_.size() - 1);
            return ids_[dist(rng)];
        }

    private:
        std::vector<OrderId> ids_;
        std::unordered_map<OrderId, std::size_t> index_;
        std::unordered_map<OrderId, Quantity> qty_;
    };

    auto apply = [](OrderBook& book, const Action& a) {
        if (a.kind == ActionKind::Add) book.add_order(a.order);
        else book.cancel_order(a.cancel_id);
    };

    struct BenchConfig {
        std::string label;    // book-size label, (small/medium/large)
        std::uint64_t seed;
        int total_ops;
        int warmup_ops;
        double p_cancel = 0.3; // lower -> fewer cancels generated -> book grows deeper
    };

    struct BenchResult {
        BenchConfig cfg;
        int measured_ops;
        std::size_t final_depth;   // resting order count at end of the timed pass
        double throughput;         // combined, ops/sec
        double add_throughput;     // ops/sec
        double cancel_throughput;  // ops/sec
        double p50, p99, p999;     // microseconds
    };

    BenchResult run_benchmark(const BenchConfig& cfg) {
        OrderFlowGenerator gen(cfg.seed, GeneratorConfig{.p_cancel = cfg.p_cancel});
        std::mt19937_64 rng(cfg.seed);

        // pregenerating orderflow
        OrderBook scratch_book;
        LivePool live; // to keep track of live orders
        std::vector<Action> actions;
        actions.reserve(cfg.total_ops);
        for (int i = 0; i < cfg.total_ops; ++i) {
            Action a = gen.next();

            if (a.kind == ActionKind::Add) {
                auto trades = scratch_book.add_order(a.order);

                Quantity traded = 0;
                for (const auto& t : trades) {
                    traded += t.quantity;
                    live.decrement(t.maker_id, t.quantity);
                }
                if (a.order.type == OrderType::GoodTillCancel && a.order.initial_quantity - traded > 0) {
                    live.add(a.order.id, a.order.initial_quantity - traded);
                }
            } else {
                if (!live.empty()) {
                    OrderId target = live.pick_random(rng);
                    a.cancel_id = target;
                    scratch_book.cancel_order(target);
                    live.remove(target);
                }
            }

            actions.push_back(a);
        }

        // 1. Measure general throughput of the engine
        OrderBook book1;
        for (int i = 0; i < cfg.warmup_ops; ++i) apply(book1, actions[i]);
        auto start_t = std::chrono::steady_clock::now();
        for (int i = cfg.warmup_ops; i < cfg.total_ops; ++i) apply(book1, actions[i]);
        auto end_t = std::chrono::steady_clock::now();
        double throughput = (cfg.total_ops - cfg.warmup_ops)
            / std::chrono::duration<double>(end_t - start_t).count();

        // 2. Measure per-op latency distribution, bucketed by action kind
        //    (bucketing here instead of a 3rd pass since filtering the stream to
        //    add-only/cancel-only would change the book's depth trajectory)
        OrderBook book2;
        for (int i = 0; i < cfg.warmup_ops; ++i) apply(book2, actions[i]);
        std::vector<double> duration_us;
        duration_us.reserve(cfg.total_ops - cfg.warmup_ops);

        double add_time_us = 0.0, cancel_time_us = 0.0;
        std::size_t add_count = 0, cancel_count = 0;

        for (int i = cfg.warmup_ops; i < cfg.total_ops; ++i) {
            auto s = std::chrono::steady_clock::now();
            apply(book2, actions[i]);
            auto e = std::chrono::steady_clock::now();

            double us = std::chrono::duration<double, std::micro>(e - s).count();
            duration_us.push_back(us);

            if (actions[i].kind == ActionKind::Add) {
                add_time_us += us;
                ++add_count;
            } else {
                cancel_time_us += us;
                ++cancel_count;
            }
        }

        double add_throughput = add_count / (add_time_us / 1e6);
        double cancel_throughput = cancel_count / (cancel_time_us / 1e6);

        std::sort(duration_us.begin(), duration_us.end());
        auto percentile = [&](double p) {
            std::size_t idx = static_cast<std::size_t>(p * (duration_us.size() - 1));
            return duration_us[idx];
        };

        return BenchResult{
            cfg,
            cfg.total_ops - cfg.warmup_ops,
            book1.resting_order_count(),
            throughput,
            add_throughput,
            cancel_throughput,
            percentile(0.50),
            percentile(0.99),
            percentile(0.999),
        };
    }

    void print_result(const BenchResult& r) {
        std::cout << std::left << std::setw(8) << r.cfg.label
                   << " seed=" << std::setw(6) << r.cfg.seed
                   << " ops=" << std::setw(10) << r.cfg.total_ops
                   << " depth=" << std::setw(9) << r.final_depth
                   << std::right << std::fixed << std::setprecision(2)
                   << "  combined=" << std::setw(7) << r.throughput / 1e6 << "M"
                   << "  add=" << std::setw(7) << r.add_throughput / 1e6 << "M"
                   << "  cancel=" << std::setw(7) << r.cancel_throughput / 1e6 << "M"
                   << std::setprecision(3)
                   << "  p50=" << std::setw(6) << r.p50
                   << "  p99=" << std::setw(6) << r.p99
                   << "  p99.9=" << std::setw(6) << r.p999 << "us"
                   << "\n";
    }

    void print_variance_summary(const std::vector<BenchResult>& results) {
        // group combined throughput (M ops/sec) by book-size label
        std::map<std::string, std::vector<double>> by_label;
        for (const auto& r : results) by_label[r.cfg.label].push_back(r.throughput / 1e6);

        std::cout << "\n----- Variance across seeds (combined throughput, M ops/sec) -----\n";
        for (const auto& [label, vals] : by_label) {
            double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
            double sq_sum = 0.0;
            for (double v : vals) sq_sum += (v - mean) * (v - mean);
            double stddev = std::sqrt(sq_sum / vals.size());
            double min_v = *std::min_element(vals.begin(), vals.end());
            double max_v = *std::max_element(vals.begin(), vals.end());

            std::cout << std::left << std::setw(8) << label
                       << std::fixed << std::setprecision(2)
                       << " mean=" << mean << "M"
                       << "  stddev=" << stddev << "M"
                       << "  min=" << min_v << "M"
                       << "  max=" << max_v << "M"
                       << "  (n=" << vals.size() << ")\n";
        }
    }
}

int main() {
    // Different (warmup, total) pairs simulate different steady-state book depths;
    // different seeds on the same size measure run-to-run variance at that size.
    // p_cancel=0.3 (the generator default) keeps every run's book nearly empty --
    // once cancels always remove a genuinely live order (LivePool), the book
    // reaches a tiny steady-state depth almost immediately regardless of total_ops.
    // p_cancel=0.05 is low enough that removals can't keep up with additions, so
    // depth instead grows roughly linearly with total_ops -- that's the actual
    // knob for "small/medium/large" book depth here.
    std::vector<BenchConfig> configs = {
        {"small",  42,  2'000'000,   100'000,   0.05},
        {"small",  7,   2'000'000,   100'000,   0.05},
        {"small",  123, 2'000'000,   100'000,   0.05},

        {"medium", 42,  8'000'000,   400'000,   0.05},
        {"medium", 7,   8'000'000,   400'000,   0.05},
        {"medium", 123, 8'000'000,   400'000,   0.05},

        {"large",  42,  20'000'000, 1'000'000,  0.05},
        {"large",  7,   20'000'000, 1'000'000,  0.05},
        {"large",  123, 20'000'000, 1'000'000,  0.05},
    };

    std::vector<BenchResult> results;
    results.reserve(configs.size());
    for (std::size_t i = 0; i < configs.size(); ++i) {
        const auto& cfg = configs[i];
        std::cout << std::defaultfloat << std::setprecision(6)
                  << "[" << (i + 1) << "/" << configs.size() << "] running "
                  << cfg.label << " seed=" << cfg.seed
                  << " ops=" << cfg.total_ops << " p_cancel=" << cfg.p_cancel
                  << " ..." << std::flush;

        auto t0 = std::chrono::steady_clock::now();
        BenchResult r = run_benchmark(cfg);
        auto t1 = std::chrono::steady_clock::now();

        std::cout << " done (" << std::fixed << std::setprecision(1)
                   << std::chrono::duration<double>(t1 - t0).count() << "s)\n" << std::flush;

        results.push_back(r);
    }

    std::cout << "\n===== OrderBook Benchmark (multi-run) =====\n";
    for (const auto& r : results) print_result(r);

    print_variance_summary(results);
    std::cout << "====================================================\n";
}