#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>

template<typename T, size_t Capacity>
class SPSCQueue {
public:
    // Capacity must be power of 2 for bitwise masking optimization
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    SPSCQueue() : head_(0), tail_(0) {}

    // Writer Thread Only
    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = current_tail + 1;

        // Check if full (Acquire load to ensure we see the latest head)
        if (next_tail - head_.load(std::memory_order_acquire) > Capacity) {
            return false;
        }

        buffer_[current_tail & (Capacity - 1)] = item;
        
        // Release: Ensures the item write is visible before the tail update
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Reader Thread Only
    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Check if empty (Acquire load to ensure we see the latest tail)
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        item = buffer_[current_head & (Capacity - 1)];

        // Release: Ensures we are done reading before updating head
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

private:
    // Align to 64 bytes to prevent False Sharing between head and tail
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    
    // Buffer storage
    T buffer_[Capacity];
};