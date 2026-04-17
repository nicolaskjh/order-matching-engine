#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
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
	[[nodiscard]] std::optional<Order> find_order(OrderID id) const;
	[[nodiscard]] Snapshot snapshot() const;

private:
	using OrderQueue = std::list<Order>;
	using OrderIter = OrderQueue::iterator;

	std::map<Price, OrderQueue, std::greater<>> bids;
	std::map<Price, OrderQueue, std::less<>> asks;
	std::unordered_map<OrderID, OrderIter> order_index;

	void match_buy(Order& incoming, std::vector<Trade>& trades);
	void match_sell(Order& incoming, std::vector<Trade>& trades);
};
