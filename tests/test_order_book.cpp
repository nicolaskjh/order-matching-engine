#include <cassert>
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <utility>
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

static void assert_step_equal(
    ReferenceOrderBook& reference_engine,
    OrderBook& your_engine,
    const Order& input_event
) {
    Order ref_incoming = input_event;
    Order uut_incoming = input_event;

    std::vector<Trade> ref_step_output;
    std::vector<Trade> uut_step_output;

    reference_engine.match(ref_incoming, ref_step_output);
    your_engine.match(uut_incoming, uut_step_output);

    assert(ref_incoming.remaining_qty == uut_incoming.remaining_qty);
    assert(ref_step_output.size() == uut_step_output.size());
    for (std::size_t i = 0; i < ref_step_output.size(); ++i) {
        assert_trade_equal(ref_step_output[i], uut_step_output[i]);
    }
}

struct DiffTestConfig {
    std::size_t snapshot_check_interval{1};
};

static void run_diff_test(const std::vector<Order>& events, const DiffTestConfig& cfg = {}) {
    ReferenceOrderBook reference_engine;
    OrderBook your_engine;

    assert_snapshot_equal(your_engine.snapshot(), reference_engine.snapshot());
    for (std::size_t i = 0; i < events.size(); ++i) {
        assert_step_equal(reference_engine, your_engine, events[i]);

        if (cfg.snapshot_check_interval > 0 && ((i + 1) % cfg.snapshot_check_interval == 0)) {
            assert_snapshot_equal(your_engine.snapshot(), reference_engine.snapshot());
        }
    }

    assert_snapshot_equal(your_engine.snapshot(), reference_engine.snapshot());
}

static void test_diff_full_fill() {
    run_diff_test({
        make_order(1, Side::SELL, OrderType::LIMIT, 101, 10, 1),
        make_order(2, Side::BUY, OrderType::LIMIT, 101, 10, 2),
    });
}

static void test_diff_fifo_same_level() {
    run_diff_test({
        make_order(21, Side::SELL, OrderType::LIMIT, 100, 4, 1),
        make_order(22, Side::SELL, OrderType::LIMIT, 100, 5, 2),
        make_order(23, Side::BUY, OrderType::LIMIT, 100, 6, 3),
    });
}

static void test_diff_partial_and_multi_level_sweep() {
    run_diff_test({
        make_order(31, Side::SELL, OrderType::LIMIT, 100, 3, 1),
        make_order(32, Side::SELL, OrderType::LIMIT, 101, 4, 2),
        make_order(33, Side::SELL, OrderType::LIMIT, 102, 5, 3),
        make_order(34, Side::BUY, OrderType::LIMIT, 101, 8, 4),
    });
}

static void test_diff_market_does_not_rest() {
    run_diff_test({
        make_order(41, Side::BUY, OrderType::MARKET, 0, 5, 1),
        make_order(42, Side::SELL, OrderType::LIMIT, 99, 5, 2),
    });
}

int main() {
    test_diff_full_fill();
    test_diff_fifo_same_level();
    test_diff_partial_and_multi_level_sweep();
    test_diff_market_does_not_rest();
    return 0;
}
