#include "message_codec.hpp"
#include "protocol_messages.hpp"
#include "unity.h"
#include <cstring>

TEST_CASE("MessageCodec can encode and decode header", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {
        .msg_type = MessageType::DATA,
        .sequence_number = 123,
        .sender_type = ReservedTypes::HUB,
        .sender_node_id = ReservedIds::HUB,
        .payload_type = 0,
        .requires_ack = true,
        .dest_node_id = 10,
        .timestamp_ms = 123456789
    };

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    size_t payload_len = sizeof(payload);

    auto encoded = codec.encode(header, payload, payload_len);

    // Total size should be Header (16) + Payload (4) + CRC (1) = 21
    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + payload_len + CRC_SIZE, encoded.size());

    auto decoded_header_opt = codec.decode_header(encoded.data(), encoded.size());
    TEST_ASSERT_TRUE(decoded_header_opt.has_value());

    MessageHeader decoded_header = decoded_header_opt.value();
    TEST_ASSERT_EQUAL(header.msg_type, decoded_header.msg_type);
    TEST_ASSERT_EQUAL(header.sequence_number, decoded_header.sequence_number);
    TEST_ASSERT_EQUAL(header.sender_type, decoded_header.sender_type);
    TEST_ASSERT_EQUAL(header.sender_node_id, decoded_header.sender_node_id);
    TEST_ASSERT_EQUAL(header.requires_ack, decoded_header.requires_ack);
    TEST_ASSERT_EQUAL(header.dest_node_id, decoded_header.dest_node_id);
    TEST_ASSERT_EQUAL(header.timestamp_ms, decoded_header.timestamp_ms);
}

TEST_CASE("MessageCodec calculates and validates CRC correctly", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = { .msg_type = MessageType::HEARTBEAT };

    auto encoded = codec.encode(header, nullptr, 0);

    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));

    // Corrupt data
    encoded[0] ^= 0xFF;
    TEST_ASSERT_FALSE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec handles empty payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = { .msg_type = MessageType::PAIR_REQUEST };

    auto encoded = codec.encode(header, nullptr, 0);

    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + CRC_SIZE, encoded.size());
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec rejects oversized payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = { .msg_type = MessageType::DATA };

    // MAX_PAYLOAD_SIZE is ESP_NOW_MAX_DATA_LEN (250) - Header (16) - CRC (1) = 233
    size_t too_big = MAX_PAYLOAD_SIZE + 1;
    uint8_t* payload = new uint8_t[too_big];
    memset(payload, 0, too_big);

    auto encoded = codec.encode(header, payload, too_big);

    TEST_ASSERT_TRUE(encoded.empty());

    delete[] payload;
}

TEST_CASE("MessageCodec decode_header fails on short data", "[codec]")
{
    RealMessageCodec codec;
    uint8_t short_data[MESSAGE_HEADER_SIZE] = {0}; // Missing CRC at least

    auto decoded = codec.decode_header(short_data, sizeof(short_data));
    TEST_ASSERT_FALSE(decoded.has_value());
}
