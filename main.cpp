#include <iostream>
#include <thread>
#include <map>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <random>

#include "order.h"
#include "ring_buffer.h"
#include "arena.h"

// Configuration
constexpr size_t MAX_ORDERS = 1000000;
constexpr const char* MCAST_GRP = "239.0.0.1";
constexpr int MCAST_PORT = 5000;

// Structures for Engine Request
struct OrderRequest {
    uint64_t id;
    uint32_t price;
    uint32_t qty;
    Side side;
};

// ---------------------------------------------------------
// NETWORK UTILS (UDP Multicast)
// ---------------------------------------------------------
int create_multicast_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    return sock;
}

void send_trade_report(int sock, struct sockaddr_in& addr, uint32_t price, uint32_t qty) {
    TradeMessage msg{price, qty};
    sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr*)&addr, sizeof(addr));
}

// ---------------------------------------------------------
// MATCHING ENGINE (The Consumer)
// ---------------------------------------------------------
void matching_engine_thread(SPSCQueue<OrderRequest, 1024>& ring_buffer) {
    // 1. Setup Memory Arena
    OrderArena arena(MAX_ORDERS);

    // 2. Setup Order Book (Price -> List of Orders)
    // We use map for Price Levels (Sparse), but Intrusive List for Orders
    std::map<uint32_t, Order*, std::greater<uint32_t>> bids; // Highest price first
    std::map<uint32_t, Order*, std::less<uint32_t>> asks;    // Lowest price first

    // 3. Setup Network (Publisher)
    int sock = create_multicast_socket();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(MCAST_GRP);
    addr.sin_port = htons(MCAST_PORT);

    std::cout << "[Engine] Initialized. Waiting for orders..." << std::endl;

    OrderRequest req;
    while (true) {
        // Busy spin (Polling) - No blocking!
        while (!ring_buffer.pop(req)) {
            // In a real HFT system, we might place a _mm_pause() here 
            // to save power, but for lowest latency, we burn CPU.
        }

        // --- MATCHING LOGIC ---
        bool executed = false;
        
        if (req.side == Side::BUY) {
            // Check Asks (Lowest Sell Price)
            auto best_ask = asks.begin();
            while (best_ask != asks.end() && best_ask->first <= req.price && req.qty > 0) {
                Order* sell_order = best_ask->second;
                
                // Match Logic
                uint32_t trade_qty = std::min(req.qty, sell_order->qty);
                uint32_t trade_price = sell_order->price;
                
                // Publish Trade
                send_trade_report(sock, addr, trade_price, trade_qty);

                // Update Quantities
                req.qty -= trade_qty;
                sell_order->qty -= trade_qty;

                // Remove filled order from Book and Arena
                if (sell_order->qty == 0) {
                    // Update head of list
                    best_ask->second = sell_order->next; 
                    if (best_ask->second) best_ask->second->prev = nullptr;
                    
                    // If list empty, remove price level
                    if (!best_ask->second) {
                        asks.erase(best_ask++);
                    } else {
                        best_ask++;
                    }
                    
                    arena.deallocate(sell_order);
                } else {
                    break; // Partial fill of sell order, we are done
                }
            }

            // If remaining quantity, add to Book
            if (req.qty > 0) {
                Order* new_order = arena.allocate();
                if (new_order) {
                    new_order->id = req.id;
                    new_order->price = req.price;
                    new_order->qty = req.qty;
                    new_order->side = Side::BUY;
                    
                    // Insert into Intrusive List at Price Level
                    Order*& list_head = bids[req.price];
                    new_order->next = list_head;
                    if (list_head) list_head->prev = new_order;
                    list_head = new_order;
                }
            }
        } 
        else { // SELL
            // Logic is symmetric for Sell side (omitted for brevity, matches against Bids)
             auto best_bid = bids.begin();
            while (best_bid != bids.end() && best_bid->first >= req.price && req.qty > 0) {
                Order* buy_order = best_bid->second;
                uint32_t trade_qty = std::min(req.qty, buy_order->qty);
                uint32_t trade_price = buy_order->price;

                send_trade_report(sock, addr, trade_price, trade_qty);

                req.qty -= trade_qty;
                buy_order->qty -= trade_qty;

                if (buy_order->qty == 0) {
                    best_bid->second = buy_order->next;
                    if (best_bid->second) best_bid->second->prev = nullptr;
                    if (!best_bid->second) bids.erase(best_bid++);
                    else best_bid++;
                    arena.deallocate(buy_order);
                } else {
                    break;
                }
            }
            if (req.qty > 0) {
                Order* new_order = arena.allocate();
                if (new_order) {
                    new_order->id = req.id;
                    new_order->price = req.price;
                    new_order->qty = req.qty;
                    new_order->side = Side::SELL;
                    Order*& list_head = asks[req.price];
                    new_order->next = list_head;
                    if (list_head) list_head->prev = new_order;
                    list_head = new_order;
                }
            }
        }
    }
}

// ---------------------------------------------------------
// MARKET SIMULATOR (The Producer)
// ---------------------------------------------------------
void udp_listener_thread(SPSCQueue<OrderRequest, 1024>& ring_buffer) {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    // Create Socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Bind to port 9999 (Where Python sends data)
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(9999);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    std::cout << "[Gateway] Listening on UDP Port 9999..." << std::endl;

    while (true) {
        OrderRequest req;
        socklen_t len = sizeof(cliaddr);
        
        // Receive Binary Data directly into the Struct
        // Note: recvfrom is blocking, which is fine for this thread.
        int n = recvfrom(sockfd, &req, sizeof(OrderRequest), 
                        MSG_WAITALL, (struct sockaddr *)&cliaddr, &len);
        
        if (n > 0) {
            // Push to Lock-Free Ring Buffer
            while (!ring_buffer.push(req)) {
                // Busy spin if buffer full (Backpressure)
            }
        }
    }
    close(sockfd);
}

int main() {
    SPSCQueue<OrderRequest, 1024> ring_buffer;

    // Launch threads
    std::thread producer(udp_listener_thread, std::ref(ring_buffer));
    std::thread consumer(matching_engine_thread, std::ref(ring_buffer));

    producer.join();
    consumer.join();

    return 0;
}