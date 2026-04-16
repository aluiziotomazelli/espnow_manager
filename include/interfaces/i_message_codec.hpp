// include/internface/i_message_codec.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "espnow_types.hpp"
#include "protocol_messages.hpp"

/**
 * @interface IMessageCodec
 * @brief Message encoding/decoding and CRC validation.
 * @internal
 */
class IMessageCodec
{
public:
    virtual ~IMessageCodec() = default;

    /**
     * @brief Encodes a message into a provided buffer.
     * @param header Message header to encode.
     * @param payload Pointer to the payload data (can be nullptr if len is 0).
     * @param len Length of the payload in bytes.
     * @param out Pointer to the destination buffer.
     * @param out_max Maximum size of the destination buffer.
     * @return Number of bytes written to 'out', or 0 if encoding failed.
     * @internal
     */
    virtual size_t encode(const MessageHeader &header, const void *payload, size_t len, uint8_t *out, size_t out_max) = 0;

    /**
     * @brief Decodes a message header from raw data.
     * @param data Pointer to the raw data.
     * @param len Length of the raw data.
     * @return Optional containing the decoded header, or nullopt if decoding failed.
     * @internal
     */
    virtual std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) = 0;

    /**
     * @brief Validates the CRC of a message.
     * @param data Pointer to the raw data.
     * @param len Length of the raw data.
     * @return true if CRC is valid, false otherwise.
     * @internal
     */
    virtual bool validate_crc(const uint8_t *data, size_t len) = 0;

    /**
     * @brief Calculates the CRC of a message.
     * @param data Pointer to the raw data.
     * @param len Length of the raw data.
     * @return Calculated CRC value.
     * @internal
     */
    virtual uint8_t calculate_crc(const uint8_t *data, size_t len) = 0;
};