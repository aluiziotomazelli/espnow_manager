// include/internface/i_message_codec.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "espnow_types.hpp"
#include "protocol_messages.hpp"

/**
 * @interface IMessageCodec
 * @brief Message encoding/decoding and CRC validation (internal)
 * @internal
 */
class IMessageCodec
{
public:
    virtual ~IMessageCodec() = default;

    /** @internal */
    virtual std::vector<uint8_t> encode(const MessageHeader &header, const void *payload, size_t len) = 0;
    /** @internal */
    virtual std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) = 0;
    /** @internal */
    virtual bool validate_crc(const uint8_t *data, size_t len) = 0;
    /** @internal */
    virtual uint8_t calculate_crc(const uint8_t *data, size_t len) = 0;
};