#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "order.hpp"
#include "trade.hpp"

class OrderBook {
public:
	struct BookLevel {
		Price price;
		Qty total_qty;
		std::size_t order_count;
	};

	struct Snapshot {
		std::vector<BookLevel> bids;
		std::vector<BookLevel> asks;
	};

	void add_order(const Order& order);
	void match(Order& incoming, std::vector<Trade>& trades);
	[[nodiscard]] Snapshot snapshot() const;

private:
	using OrderQueue = std::deque<Order>;

	std::map<Price, OrderQueue, std::greater<>> bids;
	std::map<Price, OrderQueue, std::less<>> asks;

	void match_buy(Order& incoming, std::vector<Trade>& trades);
	void match_sell(Order& incoming, std::vector<Trade>& trades);
};
