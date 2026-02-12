#include "message_codec.hpp"
#include "protocol_messages.hpp"
#include "unity.h"
#include <cstring>

TEST_CASE("MessageCodec can encode and decode header", "[codec]")
{
    RealMessageCodec codec;
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

TEST_CASE("MessageCodec CRC itegrity - All positions", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t payload[]    = {0xAA, 0xBB, 0xCC, 0xDD};
    auto encoded         = codec.encode(header, payload, sizeof(payload));

    // Verify if CRC is valid
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));

    // test corrupted CRC on each byte individually
    for (size_t i = 0; i < encoded.size(); i++) {
        uint8_t original_byte = encoded[i];

        encoded[i] ^= 0xFF; // Corrups the byte
        TEST_ASSERT_FALSE_MESSAGE(codec.validate_crc(encoded.data(), encoded.size()),
                                  "Falha: CRC nao detectou corrupcao na posicao esperada");

        encoded[i] = original_byte; // Restore original byte for next iteration
    }
}

TEST_CASE("MessageCodec detects corrupted payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t payload[]    = {0x11, 0x22, 0x33};

    auto encoded = codec.encode(header, payload, sizeof(payload));

    // Corrupt last payload byte (not CRC)
    encoded[encoded.size() - 2] ^= 0x01;

    TEST_ASSERT_FALSE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec validate_crc with tiny buffer", "[codec]")
{
    RealMessageCodec codec;
    uint8_t tiny_data[1] = {0};
    TEST_ASSERT_FALSE(codec.validate_crc(tiny_data, sizeof(tiny_data)));
}

TEST_CASE("MessageCodec handles empty payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::PAIR_REQUEST};

    auto encoded = codec.encode(header, nullptr, 0);

    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + CRC_SIZE, encoded.size());
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec handles minimum payload (1 byte)", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t single_byte  = 0x42;

    auto encoded = codec.encode(header, &single_byte, 1);

    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + 1 + CRC_SIZE, encoded.size());
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));
}

TEST_CASE("MessageCodec handles max-sized payload", "[codec]")
{
    RealMessageCodec codec;
    MessageHeader header = {.msg_type = MessageType::DATA};
    uint8_t payload[MAX_PAYLOAD_SIZE];
    memset(payload, 0xAA, sizeof(payload));

    auto encoded = codec.encode(header, payload, MAX_PAYLOAD_SIZE);
    TEST_ASSERT_FALSE(encoded.empty());
    TEST_ASSERT_TRUE(codec.validate_crc(encoded.data(), encoded.size()));
}


TEST_CASE("MessageCodec decode_header fails on short data", "[codec]")
{
    RealMessageCodec codec;
    uint8_t short_data[MESSAGE_HEADER_SIZE] = {0}; // Missing CRC at least

    auto decoded = codec.decode_header(short_data, sizeof(short_data));
    TEST_ASSERT_FALSE(decoded.has_value());
}
