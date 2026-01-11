#pragma once
#include <vector>
#include <stdexcept>
#include "order.h"

class OrderArena {
public:
    OrderArena(size_t size) {
        // Pre-allocate contiguous memory
        pool_.resize(size);
        
        // Initialize the free list stack
        for (size_t i = 0; i < size - 1; ++i) {
            pool_[i].next = &pool_[i + 1];
        }
        pool_[size - 1].next = nullptr;
        free_head_ = &pool_[0];
    }

    // O(1) Allocation
    Order* allocate() {
        if (!free_head_) return nullptr; // Pool exhausted

        Order* order = free_head_;
        free_head_ = free_head_->next;
        
        // Reset pointers
        order->next = nullptr;
        order->prev = nullptr;
        return order;
    }

    // O(1) Deallocation
    void deallocate(Order* order) {
        order->next = free_head_;
        free_head_ = order;
    }

private:
    std::vector<Order> pool_;
    Order* free_head_;
};