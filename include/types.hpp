#pragma once

#include <cstdint>

using OrderID = std::uint64_t;
using Price = std::uint32_t;
using Qty = std::uint32_t;
using Timestamp = std::uint64_t;

enum class Side : uint8_t {
    BUY,
    SELL
};

enum class OrderType : uint8_t {
    LIMIT,
    MARKET
};
