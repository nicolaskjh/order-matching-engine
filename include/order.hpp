#pragma once

#include "types.hpp"

struct Order {
    OrderID id;          
    Timestamp timestamp; 

    Price price;         
    Qty qty;             
    Qty remaining_qty;   

    Side side;           
    OrderType type;      
};
