#pragma once

#include <type_traits>
#include <vector>

#include "esp_err.h"

#include "espnow_types.hpp"

class IEspNowManager
{
public:
    virtual ~IEspNowManager() = default;

    virtual esp_err_t init(const EspNowConfig &config) = 0;
    virtual esp_err_t deinit()                         = 0;

    virtual esp_err_t send_data(NodeId dest_node_id,
                                PayloadType payload_type,
                                const void *payload,
                                size_t len,
                                bool require_ack = false) = 0;

    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(PayloadType)>>
    esp_err_t send_data(T1 dest_node_id, T2 payload_type, const void *payload, size_t len, bool require_ack = false)
    {
        return send_data(static_cast<NodeId>(dest_node_id), static_cast<PayloadType>(payload_type), payload, len,
                         require_ack);
    }

    virtual esp_err_t send_command(NodeId dest_node_id,
                                   CommandType command_type,
                                   const void *payload,
                                   size_t len,
                                   bool require_ack = false) = 0;

    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t send_command(T dest_node_id,
                           CommandType command_type,
                           const void *payload,
                           size_t len,
                           bool require_ack = false)
    {
        return send_command(static_cast<NodeId>(dest_node_id), command_type, payload, len, require_ack);
    }

    virtual esp_err_t confirm_reception(AckStatus status) = 0;

    virtual esp_err_t add_peer(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type) = 0;

    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t add_peer(T1 node_id, const uint8_t *mac, uint8_t channel, T2 type)
    {
        return add_peer(static_cast<NodeId>(node_id), mac, channel, static_cast<NodeType>(type));
    }

    virtual esp_err_t remove_peer(NodeId node_id) = 0;

    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t remove_peer(T node_id)
    {
        return remove_peer(static_cast<NodeId>(node_id));
    }

    virtual std::vector<PeerInfo> get_peers()                    = 0;
    virtual std::vector<NodeId> get_offline_peers() const        = 0;
    virtual esp_err_t start_pairing(uint32_t timeout_ms = 30000) = 0;
    virtual bool is_initialized() const                          = 0;
};
