#pragma once

#include <vector>

#include "order.hpp"
#include "order_book.hpp"
#include "trade.hpp"

class MatchingEngine {
public:
	enum class EventStatus {
		ACCEPTED,
		REJECTED_INVALID_ORDER,
	};

	struct EventResult {
		EventStatus status;
		std::vector<Trade> trades;
		Qty filled_qty;
		Qty remaining_qty;
	};

	EventResult on_order(const Order& order);
	[[nodiscard]] OrderBook::Snapshot snapshot() const;

private:
	OrderBook book_;
};

