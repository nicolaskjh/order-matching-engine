#pragma once

#include <cstdint>

using OrderID = std::uint64_t;
using Price = std::uint32_t;
using Qty = std::uint32_t;
using Timestamp = std::uint64_t;

enum class Side {
	BUY,
	SELL
};

enum class OrderType {
	LIMIT,
	MARKET
};
