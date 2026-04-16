#include <algorithm>

#include "order_book.hpp"

void OrderBook::add_order(const Order& order) {
	if (order.remaining_qty == 0) {
		return;
	}

	if (order.side == Side::BUY) {
		bids[order.price].push_back(order);
		return;
	}

	asks[order.price].push_back(order);
}

void OrderBook::match(Order& incoming, std::vector<Trade>& trades) {
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

void OrderBook::match_buy(Order& incoming, std::vector<Trade>& trades) {
	while (incoming.remaining_qty > 0 && !asks.empty()) {
		auto best_ask_it = asks.begin();
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
			asks.erase(best_ask_it);
		}
	}
}

void OrderBook::match_sell(Order& incoming, std::vector<Trade>& trades) {
	while (incoming.remaining_qty > 0 && !bids.empty()) {
		auto best_bid_it = bids.begin();
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
			bids.erase(best_bid_it);
		}
	}
}
