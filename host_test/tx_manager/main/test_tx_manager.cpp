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

    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK; // Transitions to block state

    tx_mgr.queue_packet(packet);

    WAIT_FOR_CONDITION(hal.send_packet_calls >= 1, 200);

    TEST_ASSERT_EQUAL(1, hal.send_packet_calls);
    TEST_ASSERT_EQUAL(1, fsm.on_tx_success_calls);

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager handles retries", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    // 1. Initial send
    TxPacket packet;
    packet.len = 10;
    packet.requires_ack = true;
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;

    tx_mgr.queue_packet(packet);
    WAIT_FOR_CONDITION(fsm.current_state_ret == TxState::WAITING_FOR_ACK, 200);
    TEST_ASSERT_EQUAL(1, hal.send_packet_calls);

    // 2. Trigger retry via timeout
    fsm.on_ack_timeout_ret = TxState::RETRYING;
    // We expect RealTxManager to enter RETRYING case, send packet, then call on_tx_success.
    // To prevent infinite loop, we must make sure on_tx_success returns a blocking state.
    fsm.on_tx_success_ret = TxState::WAITING_FOR_ACK;

    // Trigger notification manually
    xTaskNotify(tx_mgr.get_task_handle(), 0x40, eSetBits); // NOTIFY_ACK_TIMEOUT

    WAIT_FOR_CONDITION(hal.send_packet_calls >= 2, 200);
    TEST_ASSERT_EQUAL(2, hal.send_packet_calls);
    TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.current_state_ret);

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager handles physical fail", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    fsm.on_physical_fail_ret = TxState::IDLE; // Transitions to block state

    tx_mgr.notify_physical_fail();

    WAIT_FOR_CONDITION(fsm.on_physical_fail_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, fsm.on_physical_fail_calls);

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

    fsm.on_ack_received_ret = TxState::IDLE; // Transitions to block state
    tx_mgr.notify_logical_ack();

    WAIT_FOR_CONDITION(fsm.on_ack_received_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, fsm.on_ack_received_calls);

    tx_mgr.deinit();
}

TEST_CASE("RealTxManager handles scan", "[tx_manager]")
{
    MockTxStateMachine fsm;
    MockChannelScanner scanner;
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealTxManager tx_mgr(fsm, scanner, hal, codec);
    tx_mgr.init(2048, 2);

    // Force SCANNING via failure
    fsm.on_physical_fail_ret = TxState::SCANNING;
    scanner.scan_ret = { .channel = 5, .hub_found = true };

    tx_mgr.notify_physical_fail();

    WAIT_FOR_CONDITION(scanner.scan_calls >= 1, 200);
    TEST_ASSERT_EQUAL(1, scanner.scan_calls);
    TEST_ASSERT_EQUAL(5, hal.last_set_channel);

    tx_mgr.deinit();
}
