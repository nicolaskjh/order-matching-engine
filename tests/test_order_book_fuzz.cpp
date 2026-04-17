#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <random>
#include <unordered_set>
#include <vector>

#include "order_book.hpp"

struct RefBookLevel {
    Price price;
    Qty total_qty;
    std::size_t order_count;
};

struct RefSnapshot {
    std::vector<RefBookLevel> bids;
    std::vector<RefBookLevel> asks;
};

class ReferenceOrderBook {
public:
    void add_order(const Order& order) {
        if (order.remaining_qty == 0) {
            return;
        }

        if (order.side == Side::BUY) {
            bids_[order.price].push_back(order);
            return;
        }

        asks_[order.price].push_back(order);
    }

    void match(Order& incoming, std::vector<Trade>& trades) {
        if (incoming.remaining_qty == 0) {
            incoming.remaining_qty = incoming.qty;
        }

        if (incoming.side == Side::BUY) {
            match_buy(incoming, trades);
        } else {
            match_sell(incoming, trades);
        }

        if (incoming.type == OrderType::LIMIT && incoming.remaining_qty > 0) {
            add_order(incoming);
        }
    }

    [[nodiscard]] RefSnapshot snapshot() const {
        RefSnapshot view;

        view.bids.reserve(bids_.size());
        for (const auto& [price, queue] : bids_) {
            Qty total_qty = 0;
            for (const auto& order : queue) {
                total_qty += order.remaining_qty;
            }

            view.bids.push_back(RefBookLevel{
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

            view.asks.push_back(RefBookLevel{
                .price = price,
                .total_qty = total_qty,
                .order_count = queue.size(),
            });
        }

        return view;
    }

private:
    using OrderQueue = std::deque<Order>;

    std::map<Price, OrderQueue, std::greater<>> bids_;
    std::map<Price, OrderQueue, std::less<>> asks_;

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
                    queue.pop_front();
                }
            }

            if (queue.empty()) {
                bids_.erase(best_bid_it);
            }
        }
    }
};

class DuplicateAwareReferenceOrderBook {
public:
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

    void match(Order& incoming, std::vector<Trade>& trades) {
        if (incoming.remaining_qty == 0) {
            incoming.remaining_qty = incoming.qty;
        }

        if (incoming.side == Side::BUY) {
            match_buy(incoming, trades);
        } else {
            match_sell(incoming, trades);
        }

        if (incoming.type == OrderType::LIMIT && incoming.remaining_qty > 0) {
            add_order(incoming);
        }
    }

    [[nodiscard]] RefSnapshot snapshot() const {
        RefSnapshot view;

        view.bids.reserve(bids_.size());
        for (const auto& [price, queue] : bids_) {
            Qty total_qty = 0;
            for (const auto& order : queue) {
                total_qty += order.remaining_qty;
            }

            view.bids.push_back(RefBookLevel{
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

            view.asks.push_back(RefBookLevel{
                .price = price,
                .total_qty = total_qty,
                .order_count = queue.size(),
            });
        }

        return view;
    }

private:
    using OrderQueue = std::deque<Order>;

    std::map<Price, OrderQueue, std::greater<>> bids_;
    std::map<Price, OrderQueue, std::less<>> asks_;
    std::unordered_set<OrderID> active_ids_;

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

static void assert_snapshot_equal(const OrderBook::Snapshot& uut, const RefSnapshot& ref) {
    assert(uut.bids.size() == ref.bids.size());
    for (std::size_t i = 0; i < uut.bids.size(); ++i) {
        assert(uut.bids[i].price == ref.bids[i].price);
        assert(uut.bids[i].total_qty == ref.bids[i].total_qty);
        assert(uut.bids[i].order_count == ref.bids[i].order_count);
    }

    assert(uut.asks.size() == ref.asks.size());
    for (std::size_t i = 0; i < uut.asks.size(); ++i) {
        assert(uut.asks[i].price == ref.asks[i].price);
        assert(uut.asks[i].total_qty == ref.asks[i].total_qty);
        assert(uut.asks[i].order_count == ref.asks[i].order_count);
    }
}

static void assert_snapshot_invariants(const OrderBook::Snapshot& snapshot) {
    for (std::size_t i = 0; i < snapshot.bids.size(); ++i) {
        const auto& level = snapshot.bids[i];
        assert(level.total_qty > 0);
        assert(level.order_count > 0);
        if (i > 0) {
            assert(snapshot.bids[i - 1].price > level.price);
        }
    }

    for (std::size_t i = 0; i < snapshot.asks.size(); ++i) {
        const auto& level = snapshot.asks[i];
        assert(level.total_qty > 0);
        assert(level.order_count > 0);
        if (i > 0) {
            assert(snapshot.asks[i - 1].price < level.price);
        }
    }
}

static Order random_order(std::mt19937_64& rng, OrderID id, Timestamp ts) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 99);
    std::uniform_int_distribution<int> price_dist(1, 250);
    std::uniform_int_distribution<int> cluster_dist(0, 4);
    std::uniform_int_distribution<int> qty_dist(1, 50);

    const Side side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
    const bool is_market = type_dist(rng) < 30;
    const OrderType type = is_market ? OrderType::MARKET : OrderType::LIMIT;
    Price price = 0U;
    if (!is_market) {
        if (cluster_dist(rng) == 0) {
            price = 100U;
        } else {
            price = static_cast<Price>(price_dist(rng));
        }
    }
    const Qty qty = static_cast<Qty>(qty_dist(rng));

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

static void run_diff_sequence(const std::vector<Order>& events, std::size_t snapshot_interval) {
    ReferenceOrderBook reference;
    OrderBook uut;

    assert_snapshot_equal(uut.snapshot(), reference.snapshot());

    for (std::size_t i = 0; i < events.size(); ++i) {
        Order ref_incoming = events[i];
        Order uut_incoming = events[i];

        std::vector<Trade> ref_trades;
        std::vector<Trade> uut_trades;

        reference.match(ref_incoming, ref_trades);
        uut.match(uut_incoming, uut_trades);

        assert(ref_incoming.remaining_qty == uut_incoming.remaining_qty);
        assert(ref_trades.size() == uut_trades.size());
        for (std::size_t t = 0; t < ref_trades.size(); ++t) {
            assert_trade_equal(ref_trades[t], uut_trades[t]);
        }

        assert_snapshot_invariants(uut.snapshot());

        if (snapshot_interval > 0 && ((i + 1) % snapshot_interval == 0)) {
            assert_snapshot_equal(uut.snapshot(), reference.snapshot());
        }
    }

    assert_snapshot_equal(uut.snapshot(), reference.snapshot());
}

static void run_fuzz_round(std::uint64_t seed, std::size_t steps, std::size_t snapshot_interval) {
    std::mt19937_64 rng(seed);
    ReferenceOrderBook reference;
    OrderBook uut;

    assert_snapshot_equal(uut.snapshot(), reference.snapshot());

    OrderID next_id = 1;
    for (std::size_t i = 0; i < steps; ++i) {
        Order ref_incoming = random_order(rng, next_id, static_cast<Timestamp>(i + 1));
        if ((i + 1) % 50 == 0) {
            ref_incoming.type = OrderType::MARKET;
            ref_incoming.price = 0;
            ref_incoming.qty = 1000;
            ref_incoming.remaining_qty = 1000;
        }
        Order uut_incoming = ref_incoming;
        ++next_id;

        std::vector<Trade> ref_trades;
        std::vector<Trade> uut_trades;

        reference.match(ref_incoming, ref_trades);
        uut.match(uut_incoming, uut_trades);

        assert(ref_incoming.remaining_qty == uut_incoming.remaining_qty);
        assert(ref_trades.size() == uut_trades.size());
        for (std::size_t t = 0; t < ref_trades.size(); ++t) {
            assert_trade_equal(ref_trades[t], uut_trades[t]);
        }

        assert_snapshot_invariants(uut.snapshot());

        if (snapshot_interval > 0 && ((i + 1) % snapshot_interval == 0)) {
            assert_snapshot_equal(uut.snapshot(), reference.snapshot());
        }
    }

    assert_snapshot_equal(uut.snapshot(), reference.snapshot());
}

static void run_duplicate_id_fuzz_round(std::uint64_t seed, std::size_t steps, std::size_t snapshot_interval) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> duplicate_pick_dist(0, 99);

    DuplicateAwareReferenceOrderBook reference;
    OrderBook uut;

    assert_snapshot_equal(uut.snapshot(), reference.snapshot());

    OrderID next_unique_id = 1;
    for (std::size_t i = 0; i < steps; ++i) {
        const bool use_duplicate_id = (next_unique_id > 1) && (duplicate_pick_dist(rng) < 25);

        OrderID id = next_unique_id;
        if (use_duplicate_id) {
            std::uniform_int_distribution<OrderID> id_dist(1, next_unique_id - 1);
            id = id_dist(rng);
        } else {
            ++next_unique_id;
        }

        Order ref_incoming = random_order(rng, id, static_cast<Timestamp>(i + 1));
        if ((i + 1) % 40 == 0) {
            ref_incoming.type = OrderType::MARKET;
            ref_incoming.price = 0;
            ref_incoming.qty = 1000;
            ref_incoming.remaining_qty = 1000;
        }
        Order uut_incoming = ref_incoming;

        std::vector<Trade> ref_trades;
        std::vector<Trade> uut_trades;

        reference.match(ref_incoming, ref_trades);
        uut.match(uut_incoming, uut_trades);

        assert(ref_incoming.remaining_qty == uut_incoming.remaining_qty);
        assert(ref_trades.size() == uut_trades.size());
        for (std::size_t t = 0; t < ref_trades.size(); ++t) {
            assert_trade_equal(ref_trades[t], uut_trades[t]);
        }

        assert_snapshot_invariants(uut.snapshot());

        if (snapshot_interval > 0 && ((i + 1) % snapshot_interval == 0)) {
            assert_snapshot_equal(uut.snapshot(), reference.snapshot());
        }
    }

    assert_snapshot_equal(uut.snapshot(), reference.snapshot());
}

static void test_fuzz_diff_many_seeds() {
    constexpr std::size_t kRounds = 50;
    constexpr std::size_t kStepsPerRound = 250;

    for (std::size_t round = 0; round < kRounds; ++round) {
        run_fuzz_round(0xC0FFEEULL + round, kStepsPerRound, 25);
    }
}

static void test_fuzz_duplicate_id_policy() {
    constexpr std::size_t kRounds = 40;
    constexpr std::size_t kStepsPerRound = 200;

    for (std::size_t round = 0; round < kRounds; ++round) {
        run_duplicate_id_fuzz_round(0xD00DFEEDULL + round, kStepsPerRound, 20);
    }
}

static void test_fuzz_long_run_stress() {
    run_fuzz_round(0xDEADBEEFULL, 20000, 500);
}

static void test_fuzz_adversarial_sequences() {
    {
        std::vector<Order> events;
        events.reserve(64);

        OrderID id = 900000;
        Timestamp ts = 1;
        for (int i = 0; i < 40; ++i) {
            events.push_back(Order{
                .id = id++,
                .timestamp = ts++,
                .price = 100,
                .qty = 1,
                .remaining_qty = 1,
                .side = Side::SELL,
                .type = OrderType::LIMIT,
            });
        }

        events.push_back(Order{
            .id = id++,
            .timestamp = ts++,
            .price = 0,
            .qty = 1000,
            .remaining_qty = 1000,
            .side = Side::BUY,
            .type = OrderType::MARKET,
        });

        run_diff_sequence(events, 1);
    }

    {
        std::vector<Order> events;
        events.reserve(80);

        OrderID id = 901000;
        Timestamp ts = 1;
        for (int i = 0; i < 40; ++i) {
            events.push_back(Order{
                .id = id++,
                .timestamp = ts++,
                .price = 100,
                .qty = 1,
                .remaining_qty = 1,
                .side = Side::BUY,
                .type = OrderType::LIMIT,
            });

            events.push_back(Order{
                .id = id++,
                .timestamp = ts++,
                .price = 100,
                .qty = 1,
                .remaining_qty = 1,
                .side = Side::SELL,
                .type = OrderType::LIMIT,
            });
        }

        run_diff_sequence(events, 1);
    }

    {
        std::vector<Order> events;
        events.reserve(16);

        OrderID id = 902000;
        Timestamp ts = 1;
        events.push_back(Order{.id = id++, .timestamp = ts++, .price = 0, .qty = 5, .remaining_qty = 5, .side = Side::BUY, .type = OrderType::MARKET});
        events.push_back(Order{.id = id++, .timestamp = ts++, .price = 0, .qty = 5, .remaining_qty = 5, .side = Side::SELL, .type = OrderType::MARKET});
        events.push_back(Order{.id = id++, .timestamp = ts++, .price = 102, .qty = 3, .remaining_qty = 3, .side = Side::SELL, .type = OrderType::LIMIT});
        events.push_back(Order{.id = id++, .timestamp = ts++, .price = 103, .qty = 4, .remaining_qty = 4, .side = Side::SELL, .type = OrderType::LIMIT});
        events.push_back(Order{.id = id++, .timestamp = ts++, .price = 104, .qty = 2, .remaining_qty = 2, .side = Side::SELL, .type = OrderType::LIMIT});
        events.push_back(Order{.id = id++, .timestamp = ts++, .price = 0, .qty = 20, .remaining_qty = 20, .side = Side::BUY, .type = OrderType::MARKET});

        run_diff_sequence(events, 1);
    }

    {
        std::vector<Order> events;
        events.reserve(24);

        OrderID id = 903000;
        Timestamp ts = 1;
        for (Price price = 100; price < 110; ++price) {
            events.push_back(Order{
                .id = id++,
                .timestamp = ts++,
                .price = price,
                .qty = 2,
                .remaining_qty = 2,
                .side = Side::SELL,
                .type = OrderType::LIMIT,
            });
        }

        events.push_back(Order{
            .id = id++,
            .timestamp = ts++,
            .price = 0,
            .qty = 100,
            .remaining_qty = 100,
            .side = Side::BUY,
            .type = OrderType::MARKET,
        });

        run_diff_sequence(events, 1);
    }

    {
        std::vector<Order> events;
        events.reserve(120);

        OrderID id = 904000;
        Timestamp ts = 1;
        for (int cycle = 0; cycle < 30; ++cycle) {
            events.push_back(Order{
                .id = id++,
                .timestamp = ts++,
                .price = 101,
                .qty = 2,
                .remaining_qty = 2,
                .side = Side::SELL,
                .type = OrderType::LIMIT,
            });

            events.push_back(Order{
                .id = id++,
                .timestamp = ts++,
                .price = 101,
                .qty = 1,
                .remaining_qty = 1,
                .side = Side::BUY,
                .type = OrderType::LIMIT,
            });

            events.push_back(Order{
                .id = id++,
                .timestamp = ts++,
                .price = 101,
                .qty = 1,
                .remaining_qty = 1,
                .side = Side::BUY,
                .type = OrderType::LIMIT,
            });
        }

        run_diff_sequence(events, 1);
    }
}

int main() {
    test_fuzz_diff_many_seeds();
    test_fuzz_duplicate_id_policy();
    test_fuzz_long_run_stress();
    test_fuzz_adversarial_sequences();
    return 0;
}
