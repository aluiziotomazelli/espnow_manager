#include "unity.h"
#include "tx_manager.hpp"
#include "mock_tx_state_machine.hpp"
#include "mock_channel_scanner.hpp"
#include "mock_wifi_hal.hpp"
#include "mock_message_codec.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

// Helper to wait for a condition with timeout
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

    TEST_ASSERT_EQUAL(ESP_OK, tx_mgr.init(2048, 2));
    TEST_ASSERT_EQUAL(1, hal.set_task_to_notify_calls);
    TEST_ASSERT_NOT_NULL(hal.last_task_handle);
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

    // Setup FSM to transition to a blocking state after send
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;

    tx_mgr.queue_packet(packet);

    WAIT_FOR_CONDITION(hal.send_packet_calls >= 1, 200);

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

    // Force FSM to WAITING_FOR_ACK state so the task blocks and doesn't consume the queue
    fsm.current_state_ret = TxState::WAITING_FOR_ACK;

    tx_mgr.init(2048, 2);

    TxPacket packet;
    packet.len = 10;

    // Fill the queue (size 20)
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, tx_mgr.queue_packet(packet));
    }

    // The 21st should fail with ESP_ERR_TIMEOUT
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

    // 1. Get into WAITING_FOR_ACK
    TxPacket packet;
    packet.len = 10;
    packet.requires_ack = true;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;
    tx_mgr.queue_packet(packet);
    WAIT_FOR_CONDITION(fsm.current_state_ret == TxState::WAITING_FOR_ACK, 200);

    // 2. Trigger timeout
    fsm.on_ack_timeout_ret = TxState::IDLE; // Block in IDLE
    xTaskNotify(tx_mgr.get_task_handle(), 0x40, eSetBits); // NOTIFY_ACK_TIMEOUT

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

    // 1. Get into WAITING_FOR_ACK
    TxPacket packet;
    packet.len = 10;
    packet.requires_ack = true;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;
    tx_mgr.queue_packet(packet);
    WAIT_FOR_CONDITION(fsm.current_state_ret == TxState::WAITING_FOR_ACK, 200);
    int initial_sends = hal.send_packet_calls;

    // 2. Setup for RETRYING
    fsm.on_ack_timeout_ret = TxState::RETRYING;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK; // Back to block after retry send

    PendingAck pending = fsm.last_pending_ack.value();
    pending.retries_left = 3;
    memset(pending.packet.dest_mac, 0xCC, 6);
    fsm.set_pending_ack(pending);

    // Trigger retry via timeout
    xTaskNotify(tx_mgr.get_task_handle(), 0x40, eSetBits); // NOTIFY_ACK_TIMEOUT

    WAIT_FOR_CONDITION(hal.send_packet_calls > initial_sends, 200);

    TEST_ASSERT_EQUAL(initial_sends + 1, hal.send_packet_calls);
    TEST_ASSERT_EQUAL_MEMORY(pending.packet.dest_mac, hal.last_dest_mac, 6);

    // Verify retries_left was decremented and saved back to FSM
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

    // Trigger physical fail leading to SCANNING
    fsm.on_physical_fail_ret = TxState::SCANNING;
    scanner.scan_ret = { .channel = 5, .hub_found = true };

    tx_mgr.notify_physical_fail();

    WAIT_FOR_CONDITION(scanner.scan_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, scanner.scan_calls);
    TEST_ASSERT_EQUAL(5, hal.last_set_channel);
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

    // Put it in WAITING_FOR_ACK first
    TxPacket packet;
    packet.len = 10;
    packet.requires_ack = true;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;
    tx_mgr.queue_packet(packet);
    WAIT_FOR_CONDITION(fsm.current_state_ret == TxState::WAITING_FOR_ACK, 200);

    fsm.on_ack_received_ret = TxState::IDLE;
    tx_mgr.notify_logical_ack();

    WAIT_FOR_CONDITION(fsm.on_ack_received_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, fsm.on_ack_received_calls);

    tx_mgr.deinit();
}
