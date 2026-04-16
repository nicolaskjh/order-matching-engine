#pragma once

#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "order.hpp"
#include "trade.hpp"

class OrderBook {
public:
	void add_order(const Order& order);
	void match(Order& incoming, std::vector<Trade>& trades);

private:
	using OrderQueue = std::deque<Order>;

	std::map<Price, OrderQueue, std::greater<>> bids;
	std::map<Price, OrderQueue, std::less<>> asks;

	void match_buy(Order& incoming, std::vector<Trade>& trades);
	void match_sell(Order& incoming, std::vector<Trade>& trades);
};
