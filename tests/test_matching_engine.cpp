#include <cassert>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <unordered_set>
#include <vector>

#include "matching_engine.hpp"

static Order make_order(OrderID id, Side side, OrderType type, Price price, Qty qty, Timestamp ts) {
    return Order{
        .id = id,
        .timestamp = ts,
        .price = price,
        .qty = qty,
        .remaining_qty = qty,
        .side = side,
        .type = type,
    };
}

static void test_rejects_zero_qty() {
    MatchingEngine engine;

    const auto result = engine.on_order(make_order(1, Side::BUY, OrderType::LIMIT, 100, 0, 1));

    assert(result.status == MatchingEngine::EventStatus::REJECTED_INVALID_ORDER);
    assert(result.trades.empty());
    assert(result.filled_qty == 0);
    assert(result.remaining_qty == 0);

    const auto view = engine.snapshot();
    assert(view.bids.empty());
    assert(view.asks.empty());
}

static void test_rejects_limit_with_zero_price() {
    MatchingEngine engine;

    const auto result = engine.on_order(make_order(2, Side::SELL, OrderType::LIMIT, 0, 5, 1));

    assert(result.status == MatchingEngine::EventStatus::REJECTED_INVALID_ORDER);
    assert(result.trades.empty());
    assert(result.filled_qty == 0);
    assert(result.remaining_qty == 5);

    const auto view = engine.snapshot();
    assert(view.bids.empty());
    assert(view.asks.empty());
}

static void test_accepts_and_rests_non_crossing_limit() {
    MatchingEngine engine;

    const auto result = engine.on_order(make_order(3, Side::BUY, OrderType::LIMIT, 100, 7, 1));

    assert(result.status == MatchingEngine::EventStatus::ACCEPTED);
    assert(result.trades.empty());
    assert(result.filled_qty == 0);
    assert(result.remaining_qty == 7);

    const auto view = engine.snapshot();
    assert(view.bids.size() == 1);
    assert(view.bids[0].price == 100);
    assert(view.bids[0].total_qty == 7);
    assert(view.bids[0].order_count == 1);
    assert(view.asks.empty());
}

static void test_crossing_limit_generates_trade_and_updates_book() {
    MatchingEngine engine;

    const auto seed_result = engine.on_order(make_order(10, Side::SELL, OrderType::LIMIT, 101, 10, 1));
    assert(seed_result.status == MatchingEngine::EventStatus::ACCEPTED);

    const auto cross_result = engine.on_order(make_order(11, Side::BUY, OrderType::LIMIT, 101, 6, 2));

    assert(cross_result.status == MatchingEngine::EventStatus::ACCEPTED);
    assert(cross_result.trades.size() == 1);
    assert(cross_result.trades[0].buy_order_id == 11);
    assert(cross_result.trades[0].sell_order_id == 10);
    assert(cross_result.trades[0].price == 101);
    assert(cross_result.trades[0].qty == 6);
    assert(cross_result.filled_qty == 6);
    assert(cross_result.remaining_qty == 0);

    const auto view = engine.snapshot();
    assert(view.bids.empty());
    assert(view.asks.size() == 1);
    assert(view.asks[0].price == 101);
    assert(view.asks[0].total_qty == 4);
    assert(view.asks[0].order_count == 1);
}

static void assert_snapshot_equal(const OrderBook::Snapshot& lhs, const OrderBook::Snapshot& rhs) {
    assert(lhs.bids.size() == rhs.bids.size());
    for (std::size_t i = 0; i < lhs.bids.size(); ++i) {
        assert(lhs.bids[i].price == rhs.bids[i].price);
        assert(lhs.bids[i].total_qty == rhs.bids[i].total_qty);
        assert(lhs.bids[i].order_count == rhs.bids[i].order_count);
    }

    assert(lhs.asks.size() == rhs.asks.size());
    for (std::size_t i = 0; i < lhs.asks.size(); ++i) {
        assert(lhs.asks[i].price == rhs.asks[i].price);
        assert(lhs.asks[i].total_qty == rhs.asks[i].total_qty);
        assert(lhs.asks[i].order_count == rhs.asks[i].order_count);
    }
}

static void assert_snapshot_invariants(const OrderBook::Snapshot& snapshot) {
    for (std::size_t i = 0; i < snapshot.bids.size(); ++i) {
        assert(snapshot.bids[i].total_qty > 0);
        assert(snapshot.bids[i].order_count > 0);
        if (i > 0) {
            assert(snapshot.bids[i - 1].price > snapshot.bids[i].price);
        }
    }

    for (std::size_t i = 0; i < snapshot.asks.size(); ++i) {
        assert(snapshot.asks[i].total_qty > 0);
        assert(snapshot.asks[i].order_count > 0);
        if (i > 0) {
            assert(snapshot.asks[i - 1].price < snapshot.asks[i].price);
        }
    }
}

class ReferenceMatchingEngine {
public:
    MatchingEngine::EventResult on_order(const Order& order) {
        if (order.qty == 0) {
            return MatchingEngine::EventResult{
                .status = MatchingEngine::EventStatus::REJECTED_INVALID_ORDER,
                .trades = {},
                .filled_qty = 0,
                .remaining_qty = order.remaining_qty,
            };
        }

        if (order.type == OrderType::LIMIT && order.price == 0) {
            return MatchingEngine::EventResult{
                .status = MatchingEngine::EventStatus::REJECTED_INVALID_ORDER,
                .trades = {},
                .filled_qty = 0,
                .remaining_qty = order.remaining_qty,
            };
        }

        Order incoming = order;
        if (incoming.remaining_qty == 0) {
            incoming.remaining_qty = incoming.qty;
        }

        std::vector<Trade> trades;
        if (incoming.side == Side::BUY) {
            match_buy(incoming, trades);
        } else {
            match_sell(incoming, trades);
        }

        if (incoming.type == OrderType::LIMIT && incoming.remaining_qty > 0) {
            add_order(incoming);
        }

        return MatchingEngine::EventResult{
            .status = MatchingEngine::EventStatus::ACCEPTED,
            .trades = std::move(trades),
            .filled_qty = static_cast<Qty>(incoming.qty - incoming.remaining_qty),
            .remaining_qty = incoming.remaining_qty,
        };
    }

    [[nodiscard]] OrderBook::Snapshot snapshot() const {
        OrderBook::Snapshot view;

        view.bids.reserve(bids_.size());
        for (const auto& [price, queue] : bids_) {
            Qty total_qty = 0;
            for (const auto& order : queue) {
                total_qty += order.remaining_qty;
            }

            view.bids.push_back(OrderBook::BookLevel{
                .price = price,
                .total_qty = total_qty,
                .order_count = queue.size(),
            });
        }

        view.asks.reserve(asks_.size());
        for (const auto& [price, queue] : asks_) {
            Qty total_qty = 0;
            for (const auto& order : queue) {
                total_qty += order.remaining_qty;
            }

            view.asks.push_back(OrderBook::BookLevel{
                .price = price,
                .total_qty = total_qty,
                .order_count = queue.size(),
            });
        }

        return view;
    }

    void assert_internal_invariants() const {
        std::unordered_set<OrderID> seen;
        Qty book_qty_sum = 0;

        for (const auto& [_, queue] : bids_) {
            for (const auto& order : queue) {
                assert(order.remaining_qty > 0);
                assert(seen.insert(order.id).second);
                book_qty_sum += order.remaining_qty;
            }
        }

        for (const auto& [_, queue] : asks_) {
            for (const auto& order : queue) {
                assert(order.remaining_qty > 0);
                assert(seen.insert(order.id).second);
                book_qty_sum += order.remaining_qty;
            }
        }

        assert(seen == active_ids_);

        Qty level_qty_sum = 0;
        const auto snap = snapshot();
        for (const auto& lvl : snap.bids) {
            level_qty_sum += lvl.total_qty;
        }
        for (const auto& lvl : snap.asks) {
            level_qty_sum += lvl.total_qty;
        }
        assert(level_qty_sum == book_qty_sum);
    }

private:
    using OrderQueue = std::deque<Order>;
    std::map<Price, OrderQueue, std::greater<>> bids_;
    std::map<Price, OrderQueue, std::less<>> asks_;
    std::unordered_set<OrderID> active_ids_;

    void add_order(const Order& order) {
        if (order.remaining_qty == 0) {
            return;
        }
        if (active_ids_.find(order.id) != active_ids_.end()) {
            return;
        }

        if (order.side == Side::BUY) {
            bids_[order.price].push_back(order);
            active_ids_.insert(order.id);
            return;
        }

        asks_[order.price].push_back(order);
        active_ids_.insert(order.id);
    }

    void match_buy(Order& incoming, std::vector<Trade>& trades) {
        while (incoming.remaining_qty > 0 && !asks_.empty()) {
            auto best_ask_it = asks_.begin();
            const Price best_ask_price = best_ask_it->first;

            if (incoming.type == OrderType::LIMIT && best_ask_price > incoming.price) {
                break;
            }

            auto& queue = best_ask_it->second;
            while (incoming.remaining_qty > 0 && !queue.empty()) {
                auto& resting = queue.front();
                const Qty fill_qty = std::min(incoming.remaining_qty, resting.remaining_qty);

                trades.push_back(Trade{
                    .buy_order_id = incoming.id,
                    .sell_order_id = resting.id,
                    .timestamp = incoming.timestamp,
                    .price = resting.price,
                    .qty = fill_qty,
                });

                incoming.remaining_qty -= fill_qty;
                resting.remaining_qty -= fill_qty;

                if (resting.remaining_qty == 0) {
                    active_ids_.erase(resting.id);
                    queue.pop_front();
                }
            }

            if (queue.empty()) {
                asks_.erase(best_ask_it);
            }
        }
    }

    void match_sell(Order& incoming, std::vector<Trade>& trades) {
        while (incoming.remaining_qty > 0 && !bids_.empty()) {
            auto best_bid_it = bids_.begin();
            const Price best_bid_price = best_bid_it->first;

            if (incoming.type == OrderType::LIMIT && best_bid_price < incoming.price) {
                break;
            }

            auto& queue = best_bid_it->second;
            while (incoming.remaining_qty > 0 && !queue.empty()) {
                auto& resting = queue.front();
                const Qty fill_qty = std::min(incoming.remaining_qty, resting.remaining_qty);

                trades.push_back(Trade{
                    .buy_order_id = resting.id,
                    .sell_order_id = incoming.id,
                    .timestamp = incoming.timestamp,
                    .price = resting.price,
                    .qty = fill_qty,
                });

                incoming.remaining_qty -= fill_qty;
                resting.remaining_qty -= fill_qty;

                if (resting.remaining_qty == 0) {
                    active_ids_.erase(resting.id);
                    queue.pop_front();
                }
            }

            if (queue.empty()) {
                bids_.erase(best_bid_it);
            }
        }
    }
};

static void assert_trade_equal(const Trade& lhs, const Trade& rhs) {
    assert(lhs.buy_order_id == rhs.buy_order_id);
    assert(lhs.sell_order_id == rhs.sell_order_id);
    assert(lhs.timestamp == rhs.timestamp);
    assert(lhs.price == rhs.price);
    assert(lhs.qty == rhs.qty);
}

static void assert_result_equal(
    const MatchingEngine::EventResult& lhs,
    const MatchingEngine::EventResult& rhs
) {
    assert(lhs.status == rhs.status);
    assert(lhs.filled_qty == rhs.filled_qty);
    assert(lhs.remaining_qty == rhs.remaining_qty);
    assert(lhs.trades.size() == rhs.trades.size());
    for (std::size_t i = 0; i < lhs.trades.size(); ++i) {
        assert_trade_equal(lhs.trades[i], rhs.trades[i]);
    }
}

static bool replay_prefix_matches(
    const std::vector<Order>& events,
    std::size_t prefix_len,
    std::size_t* mismatch_index
) {
    MatchingEngine uut;
    ReferenceMatchingEngine ref;

    for (std::size_t i = 0; i < prefix_len; ++i) {
        const auto uut_result = uut.on_order(events[i]);
        const auto ref_result = ref.on_order(events[i]);

        const auto uut_snapshot = uut.snapshot();
        const auto ref_snapshot = ref.snapshot();

        const bool ok =
            (uut_result.status == ref_result.status) &&
            (uut_result.filled_qty == ref_result.filled_qty) &&
            (uut_result.remaining_qty == ref_result.remaining_qty) &&
            (uut_result.trades.size() == ref_result.trades.size()) &&
            (uut_snapshot.bids.size() == ref_snapshot.bids.size()) &&
            (uut_snapshot.asks.size() == ref_snapshot.asks.size());

        if (!ok) {
            if (mismatch_index != nullptr) {
                *mismatch_index = i;
            }
            return false;
        }

        assert_result_equal(uut_result, ref_result);
        assert_snapshot_equal(uut_snapshot, ref_snapshot);
        assert_snapshot_invariants(uut_snapshot);
        ref.assert_internal_invariants();
    }

    return true;
}

static std::optional<std::size_t> find_minimal_failing_prefix(const std::vector<Order>& events) {
    std::size_t mismatch_index = 0;
    if (replay_prefix_matches(events, events.size(), &mismatch_index)) {
        return std::nullopt;
    }

    std::size_t lo = 1;
    std::size_t hi = mismatch_index + 1;
    while (lo < hi) {
        const std::size_t mid = lo + ((hi - lo) / 2);
        if (replay_prefix_matches(events, mid, nullptr)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

static void test_fifo_multi_fill_sequence() {
    MatchingEngine engine;

    engine.on_order(make_order(100, Side::SELL, OrderType::LIMIT, 100, 3, 1));
    engine.on_order(make_order(101, Side::SELL, OrderType::LIMIT, 100, 2, 2));

    const auto result = engine.on_order(make_order(102, Side::BUY, OrderType::LIMIT, 100, 4, 3));

    assert(result.status == MatchingEngine::EventStatus::ACCEPTED);
    assert(result.trades.size() == 2);
    assert(result.trades[0].sell_order_id == 100);
    assert(result.trades[0].qty == 3);
    assert(result.trades[1].sell_order_id == 101);
    assert(result.trades[1].qty == 1);
    assert(result.filled_qty == 4);
    assert(result.remaining_qty == 0);

    const auto view = engine.snapshot();
    assert(view.asks.size() == 1);
    assert(view.asks[0].price == 100);
    assert(view.asks[0].total_qty == 1);
    assert(view.asks[0].order_count == 1);
}

static void test_partial_fill_chaining_and_level_sweep() {
    MatchingEngine engine;

    engine.on_order(make_order(200, Side::SELL, OrderType::LIMIT, 100, 3, 1));
    engine.on_order(make_order(201, Side::SELL, OrderType::LIMIT, 101, 4, 2));

    const auto result = engine.on_order(make_order(202, Side::BUY, OrderType::LIMIT, 101, 10, 3));

    assert(result.status == MatchingEngine::EventStatus::ACCEPTED);
    assert(result.trades.size() == 2);
    assert(result.trades[0].sell_order_id == 200);
    assert(result.trades[0].price == 100);
    assert(result.trades[0].qty == 3);
    assert(result.trades[1].sell_order_id == 201);
    assert(result.trades[1].price == 101);
    assert(result.trades[1].qty == 4);
    assert(result.filled_qty == 7);
    assert(result.remaining_qty == 3);

    const auto view = engine.snapshot();
    assert(view.asks.empty());
    assert(view.bids.size() == 1);
    assert(view.bids[0].price == 101);
    assert(view.bids[0].total_qty == 3);
    assert(view.bids[0].order_count == 1);
}

static void test_market_order_sweep_and_no_resting() {
    MatchingEngine engine;

    engine.on_order(make_order(300, Side::SELL, OrderType::LIMIT, 100, 2, 1));
    engine.on_order(make_order(301, Side::SELL, OrderType::LIMIT, 101, 2, 2));

    const auto result = engine.on_order(make_order(302, Side::BUY, OrderType::MARKET, 0, 5, 3));

    assert(result.status == MatchingEngine::EventStatus::ACCEPTED);
    assert(result.trades.size() == 2);
    assert(result.trades[0].price == 100);
    assert(result.trades[0].qty == 2);
    assert(result.trades[1].price == 101);
    assert(result.trades[1].qty == 2);
    assert(result.filled_qty == 4);
    assert(result.remaining_qty == 1);

    const auto view = engine.snapshot();
    assert(view.asks.empty());
    assert(view.bids.empty());
}

static void test_interleaved_state_evolution() {
    MatchingEngine engine;

    engine.on_order(make_order(400, Side::SELL, OrderType::LIMIT, 100, 3, 1));

    const auto r1 = engine.on_order(make_order(401, Side::BUY, OrderType::LIMIT, 99, 2, 2));
    assert(r1.trades.empty());

    const auto r2 = engine.on_order(make_order(402, Side::BUY, OrderType::LIMIT, 100, 1, 3));
    assert(r2.trades.size() == 1);
    assert(r2.trades[0].sell_order_id == 400);
    assert(r2.trades[0].qty == 1);

    const auto r3 = engine.on_order(make_order(403, Side::SELL, OrderType::LIMIT, 99, 2, 4));
    assert(r3.trades.size() == 1);
    assert(r3.trades[0].buy_order_id == 401);
    assert(r3.trades[0].qty == 2);

    const auto view = engine.snapshot();
    assert(view.bids.empty());
    assert(view.asks.size() == 1);
    assert(view.asks[0].price == 100);
    assert(view.asks[0].total_qty == 2);
    assert(view.asks[0].order_count == 1);
}

static void test_randomized_sequence_invariants() {
    MatchingEngine engine;
    std::mt19937_64 rng(0xA11CEULL);

    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 99);
    std::uniform_int_distribution<int> price_dist(1, 120);
    std::uniform_int_distribution<int> qty_dist(0, 20);
    std::uniform_int_distribution<int> normalize_dist(0, 9);

    OrderID next_id = 5000;
    for (int i = 0; i < 750; ++i) {
        const Side side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
        const bool is_market = type_dist(rng) < 30;
        const OrderType type = is_market ? OrderType::MARKET : OrderType::LIMIT;
        const Qty qty = static_cast<Qty>(qty_dist(rng));
        const Price price = is_market ? 0U : static_cast<Price>(price_dist(rng));

        Order order = make_order(next_id++, side, type, price, qty, static_cast<Timestamp>(i + 1));
        if (qty > 0 && normalize_dist(rng) == 0) {
            order.remaining_qty = 0;
        }

        const auto before = engine.snapshot();
        const auto result = engine.on_order(order);
        const auto after = engine.snapshot();

        if (qty == 0 || (type == OrderType::LIMIT && price == 0)) {
            assert(result.status == MatchingEngine::EventStatus::REJECTED_INVALID_ORDER);
            assert(result.trades.empty());
            assert(result.filled_qty == 0);
            assert_snapshot_equal(after, before);
        } else {
            assert(result.status == MatchingEngine::EventStatus::ACCEPTED);
            assert(result.filled_qty + result.remaining_qty == qty);
        }

        assert_snapshot_invariants(after);
    }
}

static void test_duplicate_replay_and_readd_after_fill() {
    MatchingEngine engine;

    const Order replayed = make_order(7000, Side::SELL, OrderType::LIMIT, 100, 5, 10);
    for (int i = 0; i < 5; ++i) {
        const auto result = engine.on_order(replayed);
        assert(result.status == MatchingEngine::EventStatus::ACCEPTED);
        assert(result.trades.empty());
    }

    auto view = engine.snapshot();
    assert(view.asks.size() == 1);
    assert(view.asks[0].total_qty == 5);
    assert(view.asks[0].order_count == 1);

    const auto take = engine.on_order(make_order(7001, Side::BUY, OrderType::LIMIT, 100, 5, 11));
    assert(take.trades.size() == 1);
    assert(take.trades[0].sell_order_id == 7000);
    assert(take.filled_qty == 5);

    const auto readd = engine.on_order(make_order(7000, Side::SELL, OrderType::LIMIT, 100, 3, 12));
    assert(readd.status == MatchingEngine::EventStatus::ACCEPTED);
    assert(readd.trades.empty());

    view = engine.snapshot();
    assert(view.asks.size() == 1);
    assert(view.asks[0].total_qty == 3);
    assert(view.asks[0].order_count == 1);
}

static void test_timestamp_disorder_trade_timestamp_behavior() {
    MatchingEngine engine;

    engine.on_order(make_order(7100, Side::SELL, OrderType::LIMIT, 101, 2, 100));
    const auto result = engine.on_order(make_order(7101, Side::BUY, OrderType::LIMIT, 101, 1, 50));

    assert(result.status == MatchingEngine::EventStatus::ACCEPTED);
    assert(result.trades.size() == 1);
    assert(result.trades[0].timestamp == 50);
    assert(result.trades[0].sell_order_id == 7100);
    assert(result.trades[0].buy_order_id == 7101);
}

static std::vector<Order> generate_adversarial_events() {
    std::vector<Order> events;
    events.reserve(3000);

    std::mt19937_64 rng(0xBAD5EEDULL);
    std::uniform_int_distribution<int> qty_choice(0, 99);
    std::uniform_int_distribution<int> duplicate_choice(0, 99);
    std::uniform_int_distribution<int> price_noise(0, 5);

    OrderID next_id = 90000;
    std::vector<OrderID> issued_ids;

    for (int i = 0; i < 2500; ++i) {
        const bool market_burst = (i % 40) >= 30;
        const bool long_side_run = ((i / 120) % 2) == 0;
        const bool use_buy_side = market_burst ? ((i % 2) == 0) : long_side_run;

        OrderID id = next_id;
        if (!issued_ids.empty() && duplicate_choice(rng) < 20) {
            std::uniform_int_distribution<std::size_t> pick(0, issued_ids.size() - 1);
            id = issued_ids[pick(rng)];
        } else {
            id = next_id++;
            issued_ids.push_back(id);
        }

        const Qty qty = (qty_choice(rng) < 70)
            ? static_cast<Qty>(1 + (qty_choice(rng) % 3))
            : static_cast<Qty>(200 + (qty_choice(rng) % 800));

        const bool is_market = market_burst || ((i % 17) == 0);
        const OrderType type = is_market ? OrderType::MARKET : OrderType::LIMIT;

        Price price = 0;
        if (!is_market) {
            if ((i % 25) < 18) {
                price = 100;
            } else {
                const int stair = (i / 15) % 12;
                price = static_cast<Price>(95 + stair + price_noise(rng));
            }
        }

        Timestamp ts = static_cast<Timestamp>(i + 1);
        if (i > 10 && (i % 23) == 0) {
            ts = static_cast<Timestamp>(i - 7);
        }

        events.push_back(make_order(
            id,
            use_buy_side ? Side::BUY : Side::SELL,
            type,
            price,
            qty,
            ts
        ));
    }

    return events;
}

static void test_adversarial_differential_with_replay_minimization() {
    const auto events = generate_adversarial_events();

    MatchingEngine uut;
    ReferenceMatchingEngine ref;

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto uut_result = uut.on_order(events[i]);
        const auto ref_result = ref.on_order(events[i]);

        assert_result_equal(uut_result, ref_result);

        const auto uut_snapshot = uut.snapshot();
        const auto ref_snapshot = ref.snapshot();
        assert_snapshot_equal(uut_snapshot, ref_snapshot);
        assert_snapshot_invariants(uut_snapshot);
        ref.assert_internal_invariants();
    }

    const auto minimal_prefix = find_minimal_failing_prefix(events);
    assert(!minimal_prefix.has_value());
}

int main() {
    test_rejects_zero_qty();
    test_rejects_limit_with_zero_price();
    test_accepts_and_rests_non_crossing_limit();
    test_crossing_limit_generates_trade_and_updates_book();
    test_fifo_multi_fill_sequence();
    test_partial_fill_chaining_and_level_sweep();
    test_market_order_sweep_and_no_resting();
    test_interleaved_state_evolution();
    test_randomized_sequence_invariants();
    test_duplicate_replay_and_readd_after_fill();
    test_timestamp_disorder_trade_timestamp_behavior();
    test_adversarial_differential_with_replay_minimization();
    return 0;
}
