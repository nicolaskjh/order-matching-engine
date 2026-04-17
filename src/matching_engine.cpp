#include "matching_engine.hpp"

MatchingEngine::EventResult MatchingEngine::on_order(const Order& order) {
	if (order.qty == 0) {
		return EventResult{
			.status = EventStatus::REJECTED_INVALID_ORDER,
			.trades = {},
			.filled_qty = 0,
			.remaining_qty = order.remaining_qty,
		};
	}

	if (order.type == OrderType::LIMIT && order.price == 0) {
		return EventResult{
			.status = EventStatus::REJECTED_INVALID_ORDER,
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
	book_.match(incoming, trades);

	return EventResult{
		.status = EventStatus::ACCEPTED,
		.trades = std::move(trades),
		.filled_qty = static_cast<Qty>(incoming.qty - incoming.remaining_qty),
		.remaining_qty = incoming.remaining_qty,
	};
}

OrderBook::Snapshot MatchingEngine::snapshot() const {
	return book_.snapshot();
}

