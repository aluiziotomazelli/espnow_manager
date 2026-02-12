#include "message_codec.hpp"
#include "esp_rom_crc.h"
#include <cstring>

std::vector<uint8_t> RealMessageCodec::encode(const MessageHeader &header,
                                             const void *payload,
                                             size_t len)
{
    size_t total_len = sizeof(MessageHeader) + len + CRC_SIZE;
    if (total_len > ESP_NOW_MAX_DATA_LEN)
    {
        return {};
    }

    std::vector<uint8_t> buffer(total_len);
    memcpy(buffer.data(), &header, sizeof(MessageHeader));
    if (payload && len > 0)
    {
        memcpy(buffer.data() + sizeof(MessageHeader), payload, len);
    }

    uint8_t crc = esp_rom_crc8_le(0, buffer.data(), total_len - CRC_SIZE);
    buffer.back() = crc;

    return buffer;
}

std::optional<MessageHeader> RealMessageCodec::decode_header(const uint8_t *data,
                                                            size_t len)
{
    if (len < sizeof(MessageHeader) + CRC_SIZE)
    {
        return std::nullopt;
    }

    MessageHeader header;
    memcpy(&header, data, sizeof(MessageHeader));
    return header;
}

bool RealMessageCodec::validate_crc(const uint8_t *data, size_t len)
{
    if (len < CRC_SIZE)
    {
        return false;
    }

    uint8_t received_crc = data[len - 1];
    uint8_t calculated_crc = calculate_crc(data, len - CRC_SIZE);

    return received_crc == calculated_crc;
}

uint8_t RealMessageCodec::calculate_crc(const uint8_t *data, size_t len)
{
    return esp_rom_crc8_le(0, data, len);
}
