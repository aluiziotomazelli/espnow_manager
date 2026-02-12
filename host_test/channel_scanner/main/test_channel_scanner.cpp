#include "unity.h"
#include "channel_scanner.hpp"
#include "mock_wifi_hal.hpp"
#include "mock_message_codec.hpp"
#include "protocol_types.hpp"
#include <cstring>

TEST_CASE("RealChannelScanner initialization and update_node_info", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealChannelScanner scanner(hal, codec, 10, 2);

    // Update node info and verify it doesn't crash
    scanner.update_node_info(20, 3);
}

TEST_CASE("RealChannelScanner find Hub on first channel", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    NodeId my_id = 10;
    NodeType my_type = 2;
    RealChannelScanner scanner(hal, codec, my_id, my_type);

    // Prepare mock codec to return a dummy encoded packet
    codec.encode_ret = {0xDE, 0xAD, 0xBE, 0xEF};

    // Prepare mock HAL to succeed on the first call to wait_for_event
    hal.event_responses.push(true);

    auto result = scanner.scan(1);

    TEST_ASSERT_TRUE(result.hub_found);
    TEST_ASSERT_EQUAL(1, result.channel);

    // Verify interactions
    TEST_ASSERT_EQUAL(1, hal.set_channel_calls);
    TEST_ASSERT_EQUAL(1, hal.last_set_channel);
    TEST_ASSERT_EQUAL(1, hal.send_packet_calls);
    TEST_ASSERT_EQUAL(1, hal.wait_for_event_calls);

    // Verify probe header used in encode
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

    // Fail on channel 1, 2 (2 attempts each as per SCAN_CHANNEL_ATTEMPTS)
    for (int i = 0; i < SCAN_CHANNEL_ATTEMPTS * 2; ++i) {
         hal.event_responses.push(false);
    }
    hal.event_responses.push(true); // Succeed on 3rd channel (channel 3 if started at 1)

    auto result = scanner.scan(1);

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

    // All wait_for_event return false
    hal.wait_for_event_ret = false;

    auto result = scanner.scan(1);

    TEST_ASSERT_FALSE(result.hub_found);
    TEST_ASSERT_EQUAL(13, hal.set_channel_calls); // Should have tried all 13 channels
}

TEST_CASE("RealChannelScanner handles wrap around channels", "[channel_scanner]")
{
    MockWiFiHAL hal;
    MockMessageCodec codec;
    RealChannelScanner scanner(hal, codec, 10, 2);

    codec.encode_ret = {0xDE, 0xAD, 0xBE, 0xEF};

    // Start at channel 12.
    // Try channel 12: 2 attempts fail
    // Try channel 13: 2 attempts fail
    // Try channel 1: success on first attempt
    for (int i = 0; i < SCAN_CHANNEL_ATTEMPTS * 2; ++i) {
        hal.event_responses.push(false);
    }
    hal.event_responses.push(true);

    auto result = scanner.scan(12);

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

    hal.event_responses.push(true); // Succeed immediately

    auto result = scanner.scan(99); // Invalid channel

    TEST_ASSERT_TRUE(result.hub_found);
    TEST_ASSERT_EQUAL(1, result.channel); // Should have started at 1
    TEST_ASSERT_EQUAL(1, hal.last_set_channel);
}
