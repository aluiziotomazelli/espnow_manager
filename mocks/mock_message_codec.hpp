#pragma once

#include "espnow_interfaces.hpp"
#include <vector>
#include <optional>
#include <cstring>

class MockMessageCodec : public IMessageCodec
{
public:
    // --- Variáveis para Stubbing (Controlar o comportamento) ---
    std::vector<uint8_t> encode_ret;
    std::optional<MessageHeader> decode_header_ret;
    bool validate_crc_ret = true;
    uint8_t calculate_crc_ret = 0;

    // --- Variáveis para Spying (Verificar chamadas) ---
    int encode_calls = 0;
    int decode_header_calls = 0;
    int validate_crc_calls = 0;
    int calculate_crc_calls = 0;

    MessageHeader last_encode_header = {};
    std::vector<uint8_t> last_encode_payload;
    std::vector<uint8_t> last_decode_data;
    std::vector<uint8_t> last_calculate_crc_data;

    // --- Implementação da Interface ---

    inline std::vector<uint8_t> encode(const MessageHeader &header, const void *payload, size_t len) override
    {
        encode_calls++;
        last_encode_header = header;
        if (payload && len > 0) {
            const uint8_t* p = static_cast<const uint8_t*>(payload);
            last_encode_payload.assign(p, p + len);
        } else {
            last_encode_payload.clear();
        }

        if (!encode_ret.empty()) return encode_ret;

        // Default behavior: return a buffer of appropriate size if no stub is set
        return std::vector<uint8_t>(sizeof(MessageHeader) + len + 1);
    }

    inline std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) override
    {
        decode_header_calls++;
        if (data) last_decode_data.assign(data, data + len);
        else last_decode_data.clear();
        return decode_header_ret;
    }

    inline bool validate_crc(const uint8_t *data, size_t len) override
    {
        validate_crc_calls++;
        return validate_crc_ret;
    }

    inline uint8_t calculate_crc(const uint8_t *data, size_t len) override
    {
        calculate_crc_calls++;
        if (data) last_calculate_crc_data.assign(data, data + len);
        else last_calculate_crc_data.clear();
        return calculate_crc_ret;
    }

    void reset() {
        encode_calls = 0;
        decode_header_calls = 0;
        validate_crc_calls = 0;
        calculate_crc_calls = 0;

        encode_ret.clear();
        decode_header_ret = std::nullopt;
        validate_crc_ret = true;
        calculate_crc_ret = 0;

        memset(&last_encode_header, 0, sizeof(MessageHeader));
        last_encode_payload.clear();
        last_decode_data.clear();
        last_calculate_crc_data.clear();
    }
};
