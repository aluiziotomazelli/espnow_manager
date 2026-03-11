#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "message_codec.hpp"

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
    auto encoded = codec.encode(header, payload.data(), payload.size());

    // Verify the total length
    size_t total_length = sizeof(MessageHeader) + payload.size() + CRC_SIZE;
    ASSERT_EQ(encoded.size(), total_length);

    // Decode the payload
    auto decoded = codec.decode_header(encoded.data(), encoded.size());

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
    auto encoded = codec.encode(header, nullptr, 0);

    // Verify the total length
    size_t total_length = sizeof(MessageHeader) + CRC_SIZE;
    ASSERT_EQ(encoded.size(), total_length);

    // Decode the payload
    auto decoded = codec.decode_header(encoded.data(), encoded.size());

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

    // Setup a simple payload
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

    // Encode the payload with max payload size
    auto encoded = codec.encode(header, payload.data(), MAX_PAYLOAD_SIZE);

    // Verify the total length
    EXPECT_EQ(encoded.size(), ESP_NOW_MAX_DATA_LEN);

    ASSERT_TRUE(encoded.size());
}

TEST_F(MessageCodecTest, PayloadTooLongReturnsEmptyBuffer)
{
    // Setup a valid header
    MessageHeader header = {};

    // Setup a simple payload
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

    // Encode the payload with payload size too big
    auto encoded = codec.encode(header, payload.data(), MAX_PAYLOAD_SIZE + 1);

    // Verify if the buffer is empty
    ASSERT_FALSE(encoded.size());
}

TEST_F(MessageCodecTest, DecodeWithTooSmallLenghtReturnsNullopt)
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
    auto encoded = codec.encode(header, nullptr, 0);

    encoded.back() ^= 0xFF; // corrupt the CRC

    auto decoded = codec.decode_header(encoded.data(), encoded.size());
    ASSERT_FALSE(decoded.has_value());
}

TEST_F(MessageCodecTest, ValidateCrcWithZeroLengthReturnsFalse)
{
    // Verify if the function returns false
    ASSERT_FALSE(codec.validate_crc(nullptr, 0));
}