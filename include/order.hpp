#pragma once

#include "types.hpp"

struct alignas(64) Order {
    OrderID id;          
    Timestamp timestamp; 

    Price price;         
    Qty qty;             
    Qty remaining_qty;   

    Side side;           
    OrderType type;      
};
