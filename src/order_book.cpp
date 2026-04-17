#include <algorithm>

#include "order_book.hpp"

void OrderBook::add_order(const Order& order) {
	if (order.remaining_qty == 0) {
		return;
	}

	if (order_index.find(order.id) != order_index.end()) {
		return;
	}

	if (order.side == Side::BUY) {
		auto& queue = bids[order.price];
		queue.push_back(order);
		auto it = queue.end();
		--it;
		order_index.emplace(order.id, it);
		return;
	}

	auto& queue = asks[order.price];
	queue.push_back(order);
	auto it = queue.end();
	--it;
	order_index.emplace(order.id, it);
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

std::optional<Order> OrderBook::find_order(OrderID id) const {
	const auto it = order_index.find(id);
	if (it == order_index.end()) {
		return std::nullopt;
	}

	return *it->second;
}

OrderBook::Snapshot OrderBook::snapshot() const {
	Snapshot view;

	view.bids.reserve(bids.size());
	for (const auto& [price, queue] : bids) {
		Qty total_qty = 0;
		for (const auto& order : queue) {
			total_qty += order.remaining_qty;
		}

		view.bids.push_back(BookLevel{
			.price = price,
			.total_qty = total_qty,
			.order_count = queue.size(),
		});
	}

	view.asks.reserve(asks.size());
	for (const auto& [price, queue] : asks) {
		Qty total_qty = 0;
		for (const auto& order : queue) {
			total_qty += order.remaining_qty;
		}

		view.asks.push_back(BookLevel{
			.price = price,
			.total_qty = total_qty,
			.order_count = queue.size(),
		});
	}

	return view;
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
				order_index.erase(resting.id);
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
				order_index.erase(resting.id);
				queue.pop_front();
			}
		}

		if (queue.empty()) {
			bids.erase(best_bid_it);
		}
	}
}
