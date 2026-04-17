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
    for (std::size_t i = 1; i < uut.bids.size(); ++i) {
        assert(uut.bids[i - 1].price > uut.bids[i].price);
    }
    for (std::size_t i = 1; i < uut.asks.size(); ++i) {
        assert(uut.asks[i - 1].price < uut.asks[i].price);
    }

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

static void test_lookup_finds_resting_order() {
    OrderBook book;

    Order resting = make_order(1001, Side::SELL, OrderType::LIMIT, 105, 9, 1);
    std::vector<Trade> trades;
    book.match(resting, trades);

    const auto found = book.find_order(1001);
    assert(found.has_value());
    assert(found->id == 1001);
    assert(found->side == Side::SELL);
    assert(found->price == 105);
    assert(found->remaining_qty == 9);
}

static void test_lookup_removed_after_full_fill() {
    OrderBook book;

    Order resting = make_order(2001, Side::SELL, OrderType::LIMIT, 101, 5, 1);
    std::vector<Trade> trades;
    book.match(resting, trades);

    Order aggressor = make_order(2002, Side::BUY, OrderType::LIMIT, 101, 5, 2);
    trades.clear();
    book.match(aggressor, trades);

    assert(!book.find_order(2001).has_value());
    assert(!book.find_order(2002).has_value());
}

static void test_partial_resting() {
    OrderBook book;

    Order sell1 = make_order(3001, Side::SELL, OrderType::LIMIT, 100, 10, 1);
    Order sell2 = make_order(3002, Side::SELL, OrderType::LIMIT, 100, 5, 2);

    std::vector<Trade> trades;
    book.match(sell1, trades);
    book.match(sell2, trades);

    Order buy_small = make_order(3003, Side::BUY, OrderType::LIMIT, 100, 4, 3);
    trades.clear();
    book.match(buy_small, trades);

    assert(trades.size() == 1);
    assert(trades[0].sell_order_id == 3001);
    assert(trades[0].qty == 4);

    const auto sell1_after_small = book.find_order(3001);
    const auto sell2_after_small = book.find_order(3002);
    assert(sell1_after_small.has_value());
    assert(sell2_after_small.has_value());
    assert(sell1_after_small->remaining_qty == 6);
    assert(sell2_after_small->remaining_qty == 5);

    const auto view_after_small = book.snapshot();
    assert(view_after_small.asks.size() == 1);
    assert(view_after_small.asks[0].price == 100);
    assert(view_after_small.asks[0].total_qty == 11);
    assert(view_after_small.asks[0].order_count == 2);

    Order buy_followup = make_order(3004, Side::BUY, OrderType::LIMIT, 100, 7, 4);
    trades.clear();
    book.match(buy_followup, trades);

    assert(trades.size() == 2);
    assert(trades[0].sell_order_id == 3001);
    assert(trades[0].qty == 6);
    assert(trades[1].sell_order_id == 3002);
    assert(trades[1].qty == 1);

    const auto sell1_after_followup = book.find_order(3001);
    const auto sell2_after_followup = book.find_order(3002);
    assert(!sell1_after_followup.has_value());
    assert(sell2_after_followup.has_value());
    assert(sell2_after_followup->remaining_qty == 4);
}

static void test_non_crossing_limits_rest_both() {
    OrderBook book;

    Order bid = make_order(4001, Side::BUY, OrderType::LIMIT, 99, 5, 1);
    Order ask = make_order(4002, Side::SELL, OrderType::LIMIT, 101, 7, 2);

    std::vector<Trade> bid_trades;
    std::vector<Trade> ask_trades;
    book.match(bid, bid_trades);
    book.match(ask, ask_trades);

    assert(bid_trades.empty());
    assert(ask_trades.empty());

    const auto bid_lookup = book.find_order(4001);
    const auto ask_lookup = book.find_order(4002);
    assert(bid_lookup.has_value());
    assert(ask_lookup.has_value());

    const auto view = book.snapshot();
    assert(view.bids.size() == 1);
    assert(view.bids[0].price == 99);
    assert(view.bids[0].total_qty == 5);
    assert(view.asks.size() == 1);
    assert(view.asks[0].price == 101);
    assert(view.asks[0].total_qty == 7);
}

static void test_market_order_on_empty_book() {
    OrderBook book;

    Order market_buy = make_order(5001, Side::BUY, OrderType::MARKET, 0, 8, 1);
    std::vector<Trade> trades;
    book.match(market_buy, trades);

    assert(trades.empty());
    assert(market_buy.remaining_qty == 8);
    assert(!book.find_order(5001).has_value());

    const auto view = book.snapshot();
    assert(view.bids.empty());
    assert(view.asks.empty());
}

static void test_multi_step_state_evolution() {
    OrderBook book;

    Order sell_100 = make_order(6001, Side::SELL, OrderType::LIMIT, 100, 5, 1);
    Order sell_101 = make_order(6002, Side::SELL, OrderType::LIMIT, 101, 5, 2);
    Order buy_100 = make_order(6003, Side::BUY, OrderType::LIMIT, 100, 3, 3);
    Order buy_101 = make_order(6004, Side::BUY, OrderType::LIMIT, 101, 10, 4);

    std::vector<Trade> trades;
    book.match(sell_100, trades);
    book.match(sell_101, trades);

    trades.clear();
    book.match(buy_100, trades);
    assert(trades.size() == 1);
    assert(trades[0].sell_order_id == 6001);
    assert(trades[0].qty == 3);

    trades.clear();
    book.match(buy_101, trades);
    assert(trades.size() == 2);
    assert(trades[0].sell_order_id == 6001);
    assert(trades[0].qty == 2);
    assert(trades[1].sell_order_id == 6002);
    assert(trades[1].qty == 5);

    assert(!book.find_order(6001).has_value());
    assert(!book.find_order(6002).has_value());
    const auto resting_buy = book.find_order(6004);
    assert(resting_buy.has_value());
    assert(resting_buy->remaining_qty == 3);

    const auto final_view = book.snapshot();
    assert(final_view.asks.empty());
    assert(final_view.bids.size() == 1);
    assert(final_view.bids[0].price == 101);
    assert(final_view.bids[0].total_qty == 3);
    assert(final_view.bids[0].order_count == 1);
}

static void test_duplicate_id_ignored() {
    OrderBook book;

    Order o1 = make_order(7001, Side::BUY, OrderType::LIMIT, 100, 5, 1);
    Order o2 = make_order(7001, Side::BUY, OrderType::LIMIT, 100, 10, 2);

    std::vector<Trade> trades;
    book.match(o1, trades);
    book.match(o2, trades);

    const auto found = book.find_order(7001);
    assert(found.has_value());
    assert(found->remaining_qty == 5);

    const auto view = book.snapshot();
    assert(view.bids.size() == 1);
    assert(view.bids[0].price == 100);
    assert(view.bids[0].total_qty == 5);
    assert(view.bids[0].order_count == 1);
}

static void test_zero_quantity_and_zero_remaining_edges() {
    OrderBook book;

    Order zero_qty = make_order(7101, Side::BUY, OrderType::LIMIT, 100, 0, 1);
    std::vector<Trade> trades;
    book.match(zero_qty, trades);

    assert(trades.empty());
    assert(!book.find_order(7101).has_value());

    Order seed_sell = make_order(7102, Side::SELL, OrderType::LIMIT, 100, 2, 2);
    book.match(seed_sell, trades);

    Order incoming_zero_remaining = make_order(7103, Side::BUY, OrderType::LIMIT, 100, 2, 3);
    incoming_zero_remaining.remaining_qty = 0;
    trades.clear();
    book.match(incoming_zero_remaining, trades);

    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 7103);
    assert(trades[0].sell_order_id == 7102);
    assert(trades[0].qty == 2);
    assert(incoming_zero_remaining.remaining_qty == 0);
    assert(!book.find_order(7102).has_value());
    assert(!book.find_order(7103).has_value());
}

static void test_interleaved_price_time_priority() {
    OrderBook book;

    Order sell_a = make_order(7201, Side::SELL, OrderType::LIMIT, 100, 3, 1);
    Order sell_b = make_order(7202, Side::SELL, OrderType::LIMIT, 101, 3, 2);
    Order sell_c = make_order(7203, Side::SELL, OrderType::LIMIT, 100, 3, 3);

    std::vector<Trade> trades;
    book.match(sell_a, trades);
    book.match(sell_b, trades);
    book.match(sell_c, trades);

    Order buy_100 = make_order(7204, Side::BUY, OrderType::LIMIT, 100, 5, 4);
    trades.clear();
    book.match(buy_100, trades);

    assert(trades.size() == 2);
    assert(trades[0].sell_order_id == 7201);
    assert(trades[0].price == 100);
    assert(trades[0].qty == 3);
    assert(trades[1].sell_order_id == 7203);
    assert(trades[1].price == 100);
    assert(trades[1].qty == 2);

    assert(!book.find_order(7201).has_value());
    const auto remaining_7203 = book.find_order(7203);
    assert(remaining_7203.has_value());
    assert(remaining_7203->remaining_qty == 1);
    const auto still_101 = book.find_order(7202);
    assert(still_101.has_value());
    assert(still_101->remaining_qty == 3);
}

static void test_market_sweeps_levels() {
    OrderBook book;
    std::vector<Trade> trades;

    Order sell_100 = make_order(8001, Side::SELL, OrderType::LIMIT, 100, 3, 1);
    Order sell_101 = make_order(8002, Side::SELL, OrderType::LIMIT, 101, 4, 2);
    book.match(sell_100, trades);
    book.match(sell_101, trades);

    Order market_buy = make_order(8003, Side::BUY, OrderType::MARKET, 0, 5, 3);
    trades.clear();
    book.match(market_buy, trades);

    assert(trades.size() == 2);
    assert(trades[0].price == 100);
    assert(trades[0].qty == 3);
    assert(trades[1].price == 101);
    assert(trades[1].qty == 2);
    assert(market_buy.remaining_qty == 0);

    assert(!book.find_order(8001).has_value());
    const auto remaining_8002 = book.find_order(8002);
    assert(remaining_8002.has_value());
    assert(remaining_8002->remaining_qty == 2);
    assert(!book.find_order(8003).has_value());
}

int main() {
    test_diff_full_fill();
    test_diff_fifo_same_level();
    test_diff_partial_and_multi_level_sweep();
    test_diff_market_does_not_rest();
    test_lookup_finds_resting_order();
    test_lookup_removed_after_full_fill();
    test_partial_resting();
    test_non_crossing_limits_rest_both();
    test_market_order_on_empty_book();
    test_multi_step_state_evolution();
    test_duplicate_id_ignored();
    test_zero_quantity_and_zero_remaining_edges();
    test_interleaved_price_time_priority();
    test_market_sweeps_levels();
    return 0;
}
