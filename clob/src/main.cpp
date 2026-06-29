#include "engine/OrderBook.hpp"
#include <iostream>

int main() {
    clob::OrderBook book;
    
    // Create a sample order: Buy 10 shares at $0.50 (500,000 units)
    clob::Order myOrder{1, clob::Side::Buy, 500000, 10};
    
    auto trades = book.add_order(myOrder);
    
    std::cout << "Order processed. Trades generated: " << trades.size() << std::endl;
    
    return 0;
}