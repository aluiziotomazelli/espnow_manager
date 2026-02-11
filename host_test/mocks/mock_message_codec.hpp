#pragma once

#include "espnow_interfaces.hpp"
#include <vector>
#include <optional>

class MockMessageCodec : public IMessageCodec
{
public:
    inline std::vector<uint8_t> encode(const MessageHeader &header, const void *payload, size_t len) override
    {
        return std::vector<uint8_t>();
    }
    inline std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) override
    {
        return std::nullopt;
    }
    inline bool validate_crc(const uint8_t *data, size_t len) override
    {
        return true;
    }
    inline uint8_t calculate_crc(const uint8_t *data, size_t len) override
    {
        return 0;
    }
};
