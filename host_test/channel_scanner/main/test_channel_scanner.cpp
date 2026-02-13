#include "unity.h"
#include "channel_scanner.hpp"
#include "mock_wifi_hal.hpp"
#include "mock_message_codec.hpp"
#include "protocol_types.hpp"
#include <cstring>

/**
 * @file test_channel_scanner.cpp
 * @brief Unit tests for the RealChannelScanner class.
 *
 * The Channel Scanner is responsible for finding the Hub by cycling through
 * WiFi channels and sending "Probe" messages until a response is received.
 */

TEST_CASE("RealChannelScanner initialization and update_node_info", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealChannelScanner scanner(hal, codec, 10, 2);

    // Verify that updating node information (used in probe headers) works without issues.
    scanner.update_node_info(20, 3);
}

TEST_CASE("RealChannelScanner find Hub on first channel", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    NodeId my_id = 10;
    NodeType my_type = 2;
    RealChannelScanner scanner(hal, codec, my_id, my_type);

    // Stub the codec to return a valid dummy encoded packet for the probe.
    codec.encode_ret = {0xDE, 0xAD, 0xBE, 0xEF};

    // Simulate success: the first channel (1) returns a response immediately.
    hal.event_responses.push(true);

    auto result = scanner.scan(1);

    // Verify that the scanner correctly reported finding the Hub on channel 1.
    TEST_ASSERT_TRUE(result.hub_found);
    TEST_ASSERT_EQUAL(1, result.channel);

    // Verify interactions with the hardware abstraction layer (HAL).
    TEST_ASSERT_EQUAL(1, hal.set_channel_calls);
    TEST_ASSERT_EQUAL(1, hal.last_set_channel);
    TEST_ASSERT_EQUAL(1, hal.send_packet_calls);
    TEST_ASSERT_EQUAL(1, hal.wait_for_event_calls);

    // Verify that the probe message header was correctly populated.
    TEST_ASSERT_EQUAL(MessageType::CHANNEL_SCAN_PROBE, codec.last_encode_header.msg_type);
    TEST_ASSERT_EQUAL(my_id, codec.last_encode_header.sender_node_id);
    TEST_ASSERT_EQUAL(my_type, codec.last_encode_header.sender_type);
    TEST_ASSERT_EQUAL(ReservedIds::HUB, codec.last_encode_header.dest_node_id);
}

TEST_CASE("RealChannelScanner find Hub on later channel", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealChannelScanner scanner(hal, codec, 10, 2);

    codec.encode_ret = {0xDE, 0xAD, 0xBE, 0xEF};

    // Simulate failure on channels 1 and 2 (each channel is tried twice).
    for (int i = 0; i < SCAN_CHANNEL_ATTEMPTS * 2; ++i) {
         hal.event_responses.push(false);
    }
    // Simulate success on the 3rd channel.
    hal.event_responses.push(true);

    auto result = scanner.scan(1);

    // The scanner should correctly identify channel 3 as the working one.
    TEST_ASSERT_TRUE(result.hub_found);
    TEST_ASSERT_EQUAL(3, result.channel);
    TEST_ASSERT_EQUAL(3, hal.set_channel_calls);
}

TEST_CASE("RealChannelScanner Hub not found after all 13 channels", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealChannelScanner scanner(hal, codec, 10, 2);

    codec.encode_ret = {0xDE, 0xAD, 0xBE, 0xEF};

    // Simulate a scenario where NO channel returns a response.
    hal.wait_for_event_ret = false;

    auto result = scanner.scan(1);

    // The scanner should report failure after trying the entire spectrum (13 channels).
    TEST_ASSERT_FALSE(result.hub_found);
    TEST_ASSERT_EQUAL(13, hal.set_channel_calls);
}

TEST_CASE("RealChannelScanner handles wrap around channels", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealChannelScanner scanner(hal, codec, 10, 2);

    codec.encode_ret = {0xDE, 0xAD, 0xBE, 0xEF};

    // Scenario: Start scanning at channel 12.
    // Try 12: fail
    // Try 13: fail
    // Try 1: success!
    for (int i = 0; i < SCAN_CHANNEL_ATTEMPTS * 2; ++i) {
        hal.event_responses.push(false);
    }
    hal.event_responses.push(true);

    auto result = scanner.scan(12);

    // Verify that the scanner wrapped around from 13 back to 1.
    TEST_ASSERT_TRUE(result.hub_found);
    TEST_ASSERT_EQUAL(1, result.channel);
    TEST_ASSERT_EQUAL(3, hal.set_channel_calls);
    TEST_ASSERT_EQUAL(1, hal.last_set_channel);
}

TEST_CASE("RealChannelScanner handles invalid start channel by defaulting to 1", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealChannelScanner scanner(hal, codec, 10, 2);

    hal.event_responses.push(true);

    // If an invalid channel (like 99) is passed, the scanner should fall back to 1.
    auto result = scanner.scan(99);

    TEST_ASSERT_TRUE(result.hub_found);
    TEST_ASSERT_EQUAL(1, result.channel);
    TEST_ASSERT_EQUAL(1, hal.last_set_channel);
}
