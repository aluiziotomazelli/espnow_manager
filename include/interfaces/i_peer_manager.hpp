// include/interfaces/i_peer_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "etl/vector.h"

#include "espnow_types.hpp"
#include "protocol_messages.hpp"
namespace espnow {

/**
 * @interface IPeerManager
 * @brief Peer list management.
 * @note This is an internal interface.
 *       Users should use IEspNowManager::add_peer() instead.
 * @internal
 */
class IPeerManager
{
public:
    virtual ~IPeerManager() = default;

    /**
     * @brief Adds a peer to the list.
     * @param id Node ID.
     * @param mac Pointer to 6-byte MAC address.
     * @param type Node type.
     * @param heartbeat_interval_ms Heartbeat interval in milliseconds.
     * @return ESP_OK on success.
     * @return ESP_ERR_TIMEOUT Mutex timeout.
     * @return ESP_ERR_INVALID_ARG Invalid MAC address.
     * @return Other Internal NVS and ESP-NOW errors.
     * @internal
     */
    virtual esp_err_t add(NodeId id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms = 0) = 0;

    /**
     * @brief Template for adding peer using enums.
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
     * @brief Removes a peer from the list.
     * @param id Node ID.
     * @return ESP_OK on success.
     * @return ESP_ERR_NOT_FOUND Peer not found.
     * @return ESP_ERR_TIMEOUT Mutex timeout.
     * @return Other Internal NVS and ESP-NOW errors.
     * @internal
     */
    virtual esp_err_t remove(NodeId id) = 0;

    /**
     * @brief Template for removing peer.
     * @internal
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t remove(T id)
    {
        return remove(static_cast<NodeId>(id));
    }

    /**
     * @brief Finds MAC address for a given Node ID.
     * @param id Node ID.
     * @param mac Pointer to 6-byte MAC address buffer.
     * @return true if ID was found.
     * @note Does not crash if mac argument is nullptr (can be used to find ID only).
     * @internal
     */
    virtual bool find_mac(NodeId id, uint8_t *mac) = 0;

    /**
     * @brief Template for finding MAC.
     * @internal
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    bool find_mac(T id, uint8_t *mac)
    {
        return find_mac(static_cast<NodeId>(id), mac);
    }

    /**
     * @brief Gets all registered peers.
     * @return Vector of all registered peers.
     * @internal
     */
    virtual etl::vector<PeerInfo, MAX_PEERS> get_all() = 0;

    /**
     * @brief Gets peers that haven't been seen since a timeout.
     * @param now_ms Current time in milliseconds.
     * @return Vector of offline peers.
     * @internal
     */
    virtual etl::vector<NodeId, MAX_PEERS> get_offline(int64_t now_ms) = 0;

    /**
     * @brief Updates the last seen timestamp for a peer.
     * @param id Node ID.
     * @param now_ms Current time in milliseconds.
     * @internal
     */
    virtual void update_last_seen(NodeId id, int64_t now_ms) = 0;

    /**
     * @brief Template for updating last seen.
     * @internal
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void update_last_seen(T id, int64_t now_ms)
    {
        update_last_seen(static_cast<NodeId>(id), now_ms);
    }

    /**
     * @brief Looks up node ID by MAC address.
     * @param mac 6-byte MAC address to search for.
     * @param out_id Output parameter for the found node ID. Unchanged on failure.
     * @return ESP_OK              MAC found; out_id is populated.
     * @return ESP_ERR_NOT_FOUND  MAC is not in the peer list (unexpected — indicates a bug).
     * @return ESP_ERR_TIMEOUT    Could not acquire the mutex within the deadline.
     */
    virtual esp_err_t find_node_id_by_mac(const uint8_t* mac, NodeId& out_id) = 0;

    /**
     * @brief Loads peer list from persistent storage into PeerManager list.
     * @return ESP_OK on success.
     * @return ESP_ERR_INVALID_VERSION Mismatch PersistentData::VERSION.
     * @return ESP_ERR_INVALID_CRC CRC check failed.
     * @return ESP_ERR_TIMEOUT Mutex timeout.
     * @return Other Internal NVS errors.
     * @internal
     */
    virtual esp_err_t load_peers_from_storage() = 0;
};

} // namespace espnow
