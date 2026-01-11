#pragma once
#include <cstdint>

// Enum for Side
enum class Side : uint8_t { BUY = 0, SELL = 1 };

// The Order Object
// alignas(64) ensures this struct fits exactly in a cache line (or multiple of it)
// and prevents False Sharing between threads accessing adjacent orders.
struct alignas(64) Order {
    uint64_t id;
    uint32_t price;
    uint32_t qty;
    Side side;

    // Intrusive pointers for the Doubly Linked List
    // We don't use std::list; the order itself knows its neighbors.
    Order* next = nullptr;
    Order* prev = nullptr;
};

// Structure for UDP Multicast messages
struct  TradeMessage {
    uint32_t price;
    uint32_t qty;
};