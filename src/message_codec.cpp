#include <cstring>

#include "message_codec.hpp"

#include "esp_rom_crc.h"

namespace espnow {

size_t MessageCodec::encode(const MessageHeader& header, const void* payload, size_t len, uint8_t* out, size_t out_max)
{
    size_t total_len = sizeof(MessageHeader) + len + CRC_SIZE;
    if (total_len > out_max || total_len > ESP_NOW_MAX_DATA_LEN) {
        return 0;
    }

    if (!out) {
        return 0;
    }

    memcpy(out, &header, sizeof(MessageHeader));
    if (payload && len > 0) {
        memcpy(out + sizeof(MessageHeader), payload, len);
    }

    uint8_t crc = esp_rom_crc8_le(0, out, total_len - CRC_SIZE);
    out[total_len - 1] = crc;

    return total_len;
}

std::optional<MessageHeader> MessageCodec::decode_header(const uint8_t* data, size_t len)
{
    if (len < sizeof(MessageHeader) + CRC_SIZE || !validate_crc(data, len)) {
        return std::nullopt;
    }

    MessageHeader header;
    memcpy(&header, data, sizeof(MessageHeader));
    return header;
}

bool MessageCodec::validate_crc(const uint8_t* data, size_t len)
{
    if (len <= CRC_SIZE) {
        return false;
    }

    uint8_t received_crc = data[len - 1];
    uint8_t calculated_crc = calculate_crc(data, len - CRC_SIZE);

    return received_crc == calculated_crc;
}

uint8_t MessageCodec::calculate_crc(const uint8_t* data, size_t len)
{
    return esp_rom_crc8_le(0, data, len);
}

} // namespace espnow