#pragma once

#include "types.hpp"

struct Trade {
	OrderID buy_order_id;
	OrderID sell_order_id;
	Timestamp timestamp;

	Price price;
	Qty qty;
};
