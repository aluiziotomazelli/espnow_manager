// include/internface/i_channel_scaner.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "espnow_types.hpp"
#include "protocol_messages.hpp"

/**
 * @interface IChannelScanner
 * @brief WiFi channel scanning to find HUB or optimal channel (internal)
 * @internal
 */
class IChannelScanner
{
public:
    virtual ~IChannelScanner() = default;

    /** @internal */
    struct ScanResult
    {
        uint8_t channel;
        bool hub_found;
    };

    /** @internal */
    virtual ScanResult scan(uint8_t start_channel) = 0;
    /** @internal */
    virtual void update_node_info(NodeId id, NodeType type) = 0;

    /** @internal */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    void update_node_info(T1 id, T2 type)
    {
        update_node_info(static_cast<NodeId>(id), static_cast<NodeType>(type));
    }
};