// include/interfaces/i_discovery_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "espnow_types.hpp"
#include "protocol_messages.hpp"
#include "i_channel_observer.hpp"
#include "i_tx_manager.hpp"

/**
 * @interface IDiscoveryManager
 * @brief WiFi channel scanning and discovery probe handling (internal)
 * @internal
 */
class IDiscoveryManager
{
public:
    virtual ~IDiscoveryManager() = default;

    /** @internal */
    struct ScanResult
    {
        uint8_t channel;
        bool hub_found;
    };

    /** @internal */
    virtual esp_err_t init(NodeId id, NodeType type, ITxManager &tx_mgr, IChannelObserver *observer = nullptr) = 0;

    /** @internal */
    virtual ScanResult scan(uint8_t start_channel) = 0;

    /** @internal */
    virtual void handle_probe(const RxPacket &packet) = 0;

    /** @internal */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t init(T1 id, T2 type, ITxManager &tx_mgr, IChannelObserver *observer = nullptr)
    {
        return init(static_cast<NodeId>(id), static_cast<NodeType>(type), tx_mgr, observer);
    }
};
