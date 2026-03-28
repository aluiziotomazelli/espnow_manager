#pragma once

#include "i_message_codec.hpp"

class MessageCodec : public IMessageCodec
{
public:
    /** @copydoc IMessageCodec::encode */
    size_t encode(const MessageHeader &header, const void *payload, size_t len, uint8_t *out, size_t out_max) override;

    /** @copydoc IMessageCodec::decode_header */
    std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) override;

    /** @copydoc IMessageCodec::validate_crc */
    bool validate_crc(const uint8_t *data, size_t len) override;

    /** @copydoc IMessageCodec::calculate_crc */
    uint8_t calculate_crc(const uint8_t *data, size_t len) override;
};
