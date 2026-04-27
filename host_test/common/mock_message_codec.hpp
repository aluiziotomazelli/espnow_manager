// mock_message_codec.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_message_codec.hpp"

namespace espnow {

class MockMessageCodec : public IMessageCodec
{
public:
    MOCK_METHOD(
        size_t,
        encode,
        (const MessageHeader& header, const void* payload, size_t len, uint8_t* out, size_t out_max),
        (override));
    MOCK_METHOD(std::optional<MessageHeader>, decode_header, (const uint8_t* data, size_t len), (override));
    MOCK_METHOD(bool, validate_crc, (const uint8_t* data, size_t len), (override));
    MOCK_METHOD(uint8_t, calculate_crc, (const uint8_t* data, size_t len), (override));
};

} // namespace espnow