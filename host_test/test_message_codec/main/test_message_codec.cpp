#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "message_codec.hpp"
using namespace espnow;

class MessageCodecTest : public ::testing::Test
{
protected:
    MessageCodec codec;

    NodeId ID_2 = 2;
    NodeType PEER = 0x02;
    PayloadType PAYLOAD = 0x01;
};

TEST_F(MessageCodecTest, RoundTripDecoderReturnsValidHeader)
{
    // Setup a valid header
    MessageHeader header = {};
    header.msg_type = MessageType::DATA;
    header.sender_type = PEER;
    header.sender_node_id = ID_2;

    // Setup a simple payload
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

    // Encode the payload
    uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
    size_t encoded_len = codec.encode(header, payload.data(), payload.size(), buffer, sizeof(buffer));

    // Verify the total length
    size_t expected_length = sizeof(MessageHeader) + payload.size() + CRC_SIZE;
    ASSERT_EQ(encoded_len, expected_length);

    // Decode the payload
    auto decoded = codec.decode_header(buffer, encoded_len);

    // Verify the decoded payload
    ASSERT_TRUE(decoded.has_value()); // Check if std::optional has a value

    EXPECT_EQ(decoded.value().msg_type, header.msg_type);
    EXPECT_EQ(decoded.value().sender_type, header.sender_type);
    EXPECT_EQ(decoded.value().sender_node_id, header.sender_node_id);
}

TEST_F(MessageCodecTest, NullPayloadReturnsHeader)
{
    // Setup a valid header
    MessageHeader header = {};
    header.msg_type = MessageType::DATA;
    header.sender_type = PEER;
    header.sender_node_id = ID_2;

    // Encode the payload
    uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
    size_t encoded_len = codec.encode(header, nullptr, 0, buffer, sizeof(buffer));

    // Verify the total length
    size_t expected_length = sizeof(MessageHeader) + CRC_SIZE;
    ASSERT_EQ(encoded_len, expected_length);

    // Decode the payload
    auto decoded = codec.decode_header(buffer, encoded_len);

    // Verify the decoded payload
    ASSERT_TRUE(decoded.has_value()); // Check if std::optional has a value

    EXPECT_EQ(decoded.value().msg_type, header.msg_type);
    EXPECT_EQ(decoded.value().sender_type, header.sender_type);
    EXPECT_EQ(decoded.value().sender_node_id, header.sender_node_id);
}

TEST_F(MessageCodecTest, PayloadEqualToMaxSizeReturnsValidBuffer)
{
    // Setup a valid header
    MessageHeader header = {};

    // Setup a simple payload (content doesn't matter for size test)
    uint8_t payload[MAX_PAYLOAD_SIZE] = {0};

    // Encode the payload with max payload size
    uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
    size_t encoded_len = codec.encode(header, payload, MAX_PAYLOAD_SIZE, buffer, sizeof(buffer));

    // Verify the total length
    EXPECT_EQ(encoded_len, ESP_NOW_MAX_DATA_LEN);
}

TEST_F(MessageCodecTest, PayloadTooLongReturnsZero)
{
    // Setup a valid header
    MessageHeader header = {};

    // Setup a payload that exceeds max size
    uint8_t payload[MAX_PAYLOAD_SIZE + 1] = {0};

    // Encode the payload with payload size too big
    uint8_t buffer[ESP_NOW_MAX_DATA_LEN + 1];
    size_t encoded_len = codec.encode(header, payload, MAX_PAYLOAD_SIZE + 1, buffer, sizeof(buffer));

    // Verify if the returned length is zero
    ASSERT_EQ(encoded_len, 0);
}

TEST_F(MessageCodecTest, BufferTooSmallReturnsZero)
{
    MessageHeader header = {};
    uint8_t payload[10] = {0};
    uint8_t buffer[5]; // Too small even for header

    size_t encoded_len = codec.encode(header, payload, sizeof(payload), buffer, sizeof(buffer));
    ASSERT_EQ(encoded_len, 0);
}

TEST_F(MessageCodecTest, NullOutputBufferReturnsZero)
{
    MessageHeader header = {};
    uint8_t payload[10] = {0};

    // Passing nullptr as output buffer should return 0
    size_t encoded_len = codec.encode(header, payload, sizeof(payload), nullptr, 250);
    ASSERT_EQ(encoded_len, 0);
}

TEST_F(MessageCodecTest, DecodeWithTooSmallLengthReturnsNullopt)
{
    // Set a dummy buffer
    uint8_t encoded = 0;

    // Decode the payload with small buffer
    auto decoded = codec.decode_header(&encoded, sizeof(encoded)); // Must return std::nullopt

    ASSERT_FALSE(decoded.has_value()); // Check if std::optional has a value
}

TEST_F(MessageCodecTest, DecodeWithInvalidCrcReturnsNullopt)
{
    MessageHeader header = {};
    uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
    size_t encoded_len = codec.encode(header, nullptr, 0, buffer, sizeof(buffer));
    ASSERT_GT(encoded_len, 0);

    buffer[encoded_len - 1] ^= 0xFF; // corrupt the CRC

    auto decoded = codec.decode_header(buffer, encoded_len);
    ASSERT_FALSE(decoded.has_value());
}

TEST_F(MessageCodecTest, ValidateCrcWithZeroLengthReturnsFalse)
{
    // Verify if the function returns false
    ASSERT_FALSE(codec.validate_crc(nullptr, 0));
}
