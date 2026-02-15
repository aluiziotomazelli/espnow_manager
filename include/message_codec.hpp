#pragma once

#include "espnow_interfaces.hpp"

class RealMessageCodec : public IMessageCodec
{
public:
    std::vector<uint8_t> encode(const MessageHeader &header, const void *payload, size_t len) override;

    std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) override;

    bool validate_crc(const uint8_t *data, size_t len) override;
    uint8_t calculate_crc(const uint8_t *data, size_t len) override;
};
