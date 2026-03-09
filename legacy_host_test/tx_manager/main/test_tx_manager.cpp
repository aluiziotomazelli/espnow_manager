#include "unity.h"
#include "tx_manager.hpp"
#include "mock_tx_state_machine.hpp"
#include "mock_channel_scanner.hpp"
#include "mock_wifi_hal.hpp"
#include "mock_message_codec.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

/**
 * @file test_tx_manager.cpp
 * @brief Unit tests for the RealTxManager class.
 *
 * The TxManager handles the asynchronous queueing and transmission of packets.
 * It coordinates between the Tx FSM (state management), Channel Scanner (Hub discovery),
 * and WiFi HAL (radio driver).
 */

// Helper to wait for a condition with timeout.
// Essential for testing asynchronous logic where operations happen in a background task.
#define WAIT_FOR_CONDITION(cond, timeout_ms) \
    { \
        int count = 0; \
        while (!(cond) && count < (timeout_ms)) { \
            vTaskDelay(pdMS_TO_TICKS(10)); \
            count += 10; \
        } \
    }

TEST_CASE("RealTxManager init and deinit", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);

    // Test initialization: should create the task and set up HAL notification.
    TEST_ASSERT_EQUAL(ESP_OK, tx_mgr.init(2048, 2));
    TEST_ASSERT_EQUAL(1, hal.set_task_to_notify_calls);
    TEST_ASSERT_NOT_NULL(hal.last_task_handle);

    // Test deinitialization: should stop the task and clean up resources.
    TEST_ASSERT_EQUAL(ESP_OK, tx_mgr.deinit());
}

TEST_CASE("RealTxManager sends queued packet", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    TxPacket packet;
    memset(packet.dest_mac, 0xAA, 6);
    packet.len = 10;
    packet.requires_ack = true;

    // Configure Mock FSM: after a successful send, it should move to WAITING_FOR_ACK.
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;

    // Request a transmission.
    tx_mgr.queue_packet(packet);

    // Wait for the background task to pick up and process the packet.
    WAIT_FOR_CONDITION(hal.send_packet_calls >= 1, 200);

    // Verify that the HAL was called with correct data and the FSM was notified.
    TEST_ASSERT_EQUAL(1, hal.send_packet_calls);
    TEST_ASSERT_EQUAL_MEMORY(packet.dest_mac, hal.last_dest_mac, 6);
    TEST_ASSERT_EQUAL(1, fsm.on_tx_success_calls);

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager handles full queue", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);

    // Lock the manager in WAITING_FOR_ACK state.
    // This prevents the background task from consuming the queue.
    fsm.current_state_ret = TxState::WAITING_FOR_ACK;

    tx_mgr.init(2048, 2);

    TxPacket packet;
    packet.len = 10;

    // Fill the internal FreeRTOS queue (capacity = 20).
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, tx_mgr.queue_packet(packet));
    }

    // The 21st packet should be rejected because the queue is full.
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, tx_mgr.queue_packet(packet));

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager handles ACK timeout notification", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    // 1. Setup: send a packet and enter WAITING_FOR_ACK state.
    TxPacket packet;
    packet.len = 10;
    packet.requires_ack = true;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;
    tx_mgr.queue_packet(packet);
    WAIT_FOR_CONDITION(fsm.current_state_ret == TxState::WAITING_FOR_ACK, 200);

    // 2. Action: Manually trigger an ACK timeout (normally done by an internal timer).
    fsm.on_ack_timeout_ret = TxState::IDLE;
    xTaskNotify(tx_mgr.get_task_handle(), 0x40, eSetBits); // BIT: NOTIFY_ACK_TIMEOUT

    // Verify that the FSM was notified of the timeout.
    WAIT_FOR_CONDITION(fsm.on_ack_timeout_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, fsm.on_ack_timeout_calls);

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager RETRYING state logic", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    // 1. Get into the initial WAITING_FOR_ACK state.
    TxPacket packet;
    packet.len = 10;
    packet.requires_ack = true;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;
    tx_mgr.queue_packet(packet);
    WAIT_FOR_CONDITION(fsm.current_state_ret == TxState::WAITING_FOR_ACK, 200);
    int initial_sends = hal.send_packet_calls;

    // 2. Setup: Configure FSM to request a retry upon timeout.
    fsm.on_ack_timeout_ret = TxState::RETRYING;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;

    // Prepare mock data for the retry.
    PendingAck pending = fsm.last_pending_ack.value();
    pending.retries_left = 3;
    memset(pending.packet.dest_mac, 0xCC, 6);
    fsm.set_pending_ack(pending);

    // Trigger the timeout logic.
    xTaskNotify(tx_mgr.get_task_handle(), 0x40, eSetBits);

    // Verify: The manager should have re-sent the packet.
    WAIT_FOR_CONDITION(hal.send_packet_calls > initial_sends, 200);
    TEST_ASSERT_EQUAL(initial_sends + 1, hal.send_packet_calls);
    TEST_ASSERT_EQUAL_MEMORY(pending.packet.dest_mac, hal.last_dest_mac, 6);

    // Verify: The retry counter was decremented and the new state saved back to FSM.
    TEST_ASSERT_TRUE(fsm.last_pending_ack.has_value());
    TEST_ASSERT_EQUAL(2, fsm.last_pending_ack->retries_left);

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager handles physical fail and scan", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    // Setup: Configure FSM to trigger a channel scan upon physical failure.
    fsm.on_physical_fail_ret = TxState::SCANNING;
    // Stub scanner to "find" the Hub on channel 5.
    scanner.scan_ret = { .channel = 5, .hub_found = true };

    // Action: Notify a physical layer failure.
    tx_mgr.notify_physical_fail();

    // Verify: The background task should have performed a scan.
    WAIT_FOR_CONDITION(scanner.scan_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, scanner.scan_calls);

    // Verify: Upon finding the Hub, the manager should update the radio channel.
    TEST_ASSERT_EQUAL(5, hal.last_set_channel);

    // Verify: FSM notified that link is back alive.
    TEST_ASSERT_EQUAL(1, fsm.on_link_alive_calls);

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager handles logical ACK", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    // 1. Initial setup: send packet and wait for ACK.
    TxPacket packet;
    packet.len = 10;
    packet.requires_ack = true;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;
    tx_mgr.queue_packet(packet);
    WAIT_FOR_CONDITION(fsm.current_state_ret == TxState::WAITING_FOR_ACK, 200);

    // 2. Action: Notify arrival of a logical ACK (response from the peer app).
    fsm.on_ack_received_ret = TxState::IDLE;
    tx_mgr.notify_logical_ack();

    // Verify: FSM notified and returned to IDLE.
    WAIT_FOR_CONDITION(fsm.on_ack_received_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, fsm.on_ack_received_calls);

    tx_mgr.deinit();
}
