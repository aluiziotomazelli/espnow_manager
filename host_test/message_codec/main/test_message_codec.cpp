#include "message_codec.hpp"
#include "mock_message_codec.hpp"
#include "protocol_messages.hpp"
#include "unity.h"
#include <cstring>

/**
 * @file test_message_codec.cpp
 * @brief Unit tests for the RealMessageCodec class.
 *
 * The MessageCodec is responsible for transforming C++ protocol structures into
 * byte arrays (serialization) and back (deserialization), while handling CRC
 * validation to ensure data integrity over the air.
 */

TEST_CASE("MessageCodec can encode and decode header", "[codec]")
{
    RealMessageCodec codec;

    // Prepare a test header with various fields populated.
    MessageHeader header = {.msg_type        = MessageType::DATA,
                            .sequence_number = 123,
                            .sender_type     = ReservedTypes::HUB,
                            .sender_node_id  = ReservedIds::HUB,
                            .payload_type    = 0,
                            .requires_ack    = true,
                            .dest_node_id    = 10,
                            .timestamp_ms    = 123456789};

    uint8_t payload[]  = {0xDE, 0xAD, 0xBE, 0xEF};
    size_t payload_len = sizeof(payload);

    // Serialization process: Header + Payload -> Vector of bytes.
    auto encoded = codec.encode(header, payload, payload_len);

    // Verify the total size calculation (Header=16, Payload=4, CRC=1).
    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + payload_len + CRC_SIZE, encoded.size());

    // Deserialization process: Byte array -> Header structure.
    auto decoded_header_opt = codec.decode_header(encoded.data(), encoded.size());
    TEST_ASSERT_TRUE(decoded_header_opt.has_value());

    // Verify that every decoded field matches what was originally encoded.
    MessageHeader decoded_header = decoded_header_opt.value();
    TEST_ASSERT_EQUAL(header.msg_type, decoded_header.msg_type);
    TEST_ASSERT_EQUAL(header.sequence_number, decoded_header.sequence_number);
    TEST_ASSERT_EQUAL(header.sender_type, decoded_header.sender_type);
    TEST_ASSERT_EQUAL(header.sender_node_id, decoded_header.sender_node_id);
    TEST_ASSERT_EQUAL(header.requires_ack, decoded_header.requires_ack);
    TEST_ASSERT_EQUAL(header.dest_node_id, decoded_header.dest_node_id);
    TEST_ASSERT_EQUAL(header.timestamp_ms, decoded_header.timestamp_ms);
}

TEST_CASE("MessageCodec CRC integrity - All positions", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t payload[]    = {0xAA, 0xBB, 0xCC, 0xDD};
    auto encoded         = codec.encode(header, payload, sizeof(payload));

    // Initially, the encoded buffer should have a valid CRC.
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));

    // Robustness test: corrupt every single byte position individually
    // and verify that the CRC validation detects it every time.
    for (size_t i = 0; i < encoded.size(); i++) {
        uint8_t original_byte = encoded[i];

        encoded[i] ^= 0xFF; // Flip all bits in this byte.
        TEST_ASSERT_FALSE_MESSAGE(codec.validate_crc(encoded.data(), encoded.size()),
                                  "Fail: CRC did not detect corruption at the expected position");

        encoded[i] = original_byte; // Restore for next iteration.
    }
}

TEST_CASE("MessageCodec detects corrupted payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t payload[]    = {0x11, 0x22, 0x33};

    auto encoded = codec.encode(header, payload, sizeof(payload));

    // Corrupt one byte of the payload (the second to last byte in the buffer).
    encoded[encoded.size() - 2] ^= 0x01;

    // Verify that the CRC check fails.
    TEST_ASSERT_FALSE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec validate_crc with tiny buffer", "[codec]")
{
    RealMessageCodec codec;
    // A buffer smaller than the header + CRC is always invalid.
    uint8_t tiny_data[1] = {0};
    TEST_ASSERT_FALSE(codec.validate_crc(tiny_data, sizeof(tiny_data)));
}

TEST_CASE("MessageCodec handles empty payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::PAIR_REQUEST};

    // Encoding a header with no extra payload.
    auto encoded = codec.encode(header, nullptr, 0);

    // Should only contain Header (16) + CRC (1) = 17 bytes.
    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + CRC_SIZE, encoded.size());
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec handles minimum payload (1 byte)", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t single_byte  = 0x42;

    auto encoded = codec.encode(header, &single_byte, 1);

    // Total size: 16 (Header) + 1 (Payload) + 1 (CRC) = 18.
    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + 1 + CRC_SIZE, encoded.size());
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec rejects oversized payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};

    // MAX_PAYLOAD_SIZE is the physical limit of ESP-NOW minus our protocol overhead.
    size_t too_big   = MAX_PAYLOAD_SIZE + 1;
    uint8_t *payload = new uint8_t[too_big];
    memset(payload, 0, too_big);

    // Serialization should fail and return an empty buffer.
    auto encoded = codec.encode(header, payload, too_big);

    TEST_ASSERT_TRUE(encoded.empty());

    delete[] payload;
}

TEST_CASE("MessageCodec handles max-sized payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t payload[MAX_PAYLOAD_SIZE];
    memset(payload, 0xAA, sizeof(payload));

    // Verify we can encode a payload that exactly matches the maximum limit.
    auto encoded = codec.encode(header, payload, MAX_PAYLOAD_SIZE);
    TEST_ASSERT_FALSE(encoded.empty());
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));
}


TEST_CASE("MessageCodec decode_header fails on short data", "[codec]")
{
    RealMessageCodec codec;
    // Missing the mandatory CRC byte at the end.
    uint8_t short_data[MESSAGE_HEADER_SIZE] = {0};

    auto decoded = codec.decode_header(short_data, sizeof(short_data));
    TEST_ASSERT_FALSE(decoded.has_value());
}

TEST_CASE("MockMessageCodec spying and stubbing works", "[codec_mock]")
{
    // Verification of the Mock class functionality for use in other component tests.
    MockMessageCodec mock;

    // Testing the 'spying' capability (capturing arguments).
    MessageHeader h = {.sequence_number = 42};
    uint8_t payload[] = {1, 2, 3};
    mock.encode(h, payload, 3);

    TEST_ASSERT_EQUAL(1, mock.encode_calls);
    TEST_ASSERT_EQUAL(42, mock.last_encode_header.sequence_number);
    TEST_ASSERT_EQUAL(3, mock.last_encode_payload.size());
    TEST_ASSERT_EQUAL(1, mock.last_encode_payload[0]);

    // Testing the 'stubbing' capability (controlling return values).
    MessageHeader h2 = {.sequence_number = 100};
    mock.decode_header_ret = h2;
    uint8_t dummy_data[] = {0, 1, 2};
    auto decoded = mock.decode_header(dummy_data, 3);

    TEST_ASSERT_EQUAL(1, mock.decode_header_calls);
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL(100, decoded->sequence_number);
    TEST_ASSERT_EQUAL(3, mock.last_decode_data.size());

    // Verify that 'reset' clears internal state for test isolation.
    mock.reset();
    TEST_ASSERT_EQUAL(0, mock.encode_calls);
    TEST_ASSERT_FALSE(mock.decode_header_ret.has_value());
    TEST_ASSERT_EQUAL(0, mock.last_decode_data.size());
}
