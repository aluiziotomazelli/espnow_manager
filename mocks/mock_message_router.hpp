#pragma once

#include "espnow_interfaces.hpp"

/**
 * @brief Mock for IMessageRouter
 *
 * Used to test components that depend on the router, or to simulate
 * routing behavior in integration tests.
 */
class MockMessageRouter : public IMessageRouter
{
public:
    // --- Spying variables ---
    int handle_packet_calls = 0;
    RxPacket last_rx_packet = {};

    int set_app_queue_calls = 0;
    QueueHandle_t last_app_queue = nullptr;

    int set_node_info_calls = 0;
    NodeId last_my_id = 0;
    NodeType last_my_type = 0;

    // --- Simulation / Stubbing ---
    bool simulate_queue_full = false;
    bool should_dispatch_to_worker_ret = true;

    // --- Interface Implementation ---

    inline void handle_packet(const RxPacket &packet) override
    {
        handle_packet_calls++;
        last_rx_packet = packet;
        // Mock doesn't actually dispatch unless programmed to do so
    }

    inline bool should_dispatch_to_worker(MessageType type) override
    {
        return should_dispatch_to_worker_ret;
    }

    inline void set_app_queue(QueueHandle_t app_queue) override
    {
        set_app_queue_calls++;
        last_app_queue = app_queue;
    }

    inline void set_node_info(NodeId id, NodeType type) override
    {
        set_node_info_calls++;
        last_my_id = id;
        last_my_type = type;
    }

    // --- Test Helpers ---

    void reset()
    {
        handle_packet_calls = 0;
        last_rx_packet = {};
        set_app_queue_calls = 0;
        last_app_queue = nullptr;
        set_node_info_calls = 0;
        last_my_id = 0;
        last_my_type = 0;
        simulate_queue_full = false;
        should_dispatch_to_worker_ret = true;
    }

    /**
     * @brief Simulates a flood of messages of a certain type
     *
     * In a real test of the Router, you would call handle_packet on the REAL router multiple times.
     * This helper in the MOCK is useful if some other component dispatches through the mock.
     */
    void simulate_flood(MessageType type, int count)
    {
        RxPacket p;
        // Minimal valid-looking packet for the mock
        p.len = sizeof(MessageHeader);
        MessageHeader* h = reinterpret_cast<MessageHeader*>(p.data);
        memset(h, 0, sizeof(MessageHeader));
        h->msg_type = type;

        for (int i = 0; i < count; ++i) {
            handle_packet(p);
        }
    }
};
