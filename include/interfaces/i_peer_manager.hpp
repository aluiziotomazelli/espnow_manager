// include/interfaces/i_peer_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "etl/vector.h"

#include "espnow_types.hpp"
#include "protocol_messages.hpp"

/**
 * @interface IPeerManager
 * @brief Peer list management (internal)
 *
 * @note This is an internal interface.
 *       Users should use IEspNowManager::add_peer() instead.
 */
class IPeerManager
{
public:
    virtual ~IPeerManager() = default;

    /**
     * @brief Add peer to list
     * @internal
     */
    virtual esp_err_t add(NodeId id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms = 0) = 0;

    /**
     * @brief Template for adding peer using enums
     * @internal
     */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t add(T1 id, const uint8_t *mac, T2 type, uint32_t heartbeat_interval_ms = 0)
    {
        return add(static_cast<NodeId>(id), mac, static_cast<NodeType>(type), heartbeat_interval_ms);
    }

    /**
     * @brief Remove peer from list
     * @internal
     */
    virtual esp_err_t remove(NodeId id) = 0;

    /**
     * @brief Template for removing peer
     * @internal
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t remove(T id)
    {
        return remove(static_cast<NodeId>(id));
    }

    /**
     * @brief Find MAC address for a given Node ID
     * @param id Node ID
     * @param mac Pointer to 6-byte MAC address: uint8_t mac[6]
     * @return True if ID was found
     *
     * @note Dont crash if mac argument is nullptr, can be used for find ID only
     *
     * @internal
     */
    virtual bool find_mac(NodeId id, uint8_t *mac) = 0;

    /**
     * @brief Template for finding MAC
     * @internal
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    bool find_mac(T id, uint8_t *mac)
    {
        return find_mac(static_cast<NodeId>(id), mac);
    }

    /**
     * @brief Get all registered peers
     * @internal
     */
    virtual etl::vector<PeerInfo, MAX_PEERS> get_all() = 0;

    /**
     * @brief Get peers that haven't been seen since a timeout
     * @internal
     */
    virtual etl::vector<NodeId, MAX_PEERS> get_offline(uint64_t now_ms) = 0;

    /**
     * @brief Update the last seen timestamp for a peer
     * @internal
     */
    virtual void update_last_seen(NodeId id, uint64_t now_ms) = 0;

    /**
     * @brief Template for updating last seen
     * @internal
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void update_last_seen(T id, uint64_t now_ms)
    {
        update_last_seen(static_cast<NodeId>(id), now_ms);
    }

    /**
     * @brief Load peer list from persistent storage
     * @note clears the current peer list - intended for initialization only
     * @internal
     */
    virtual esp_err_t load_from_storage(uint8_t &wifi_channel) = 0;
};
