#pragma once

#include <optional>
#include <type_traits>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"

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
    virtual esp_err_t add(NodeId id,
                          const uint8_t *mac,
                          uint8_t channel,
                          NodeType type,
                          uint32_t heartbeat_interval_ms = 0) = 0;

    /**
     * @brief Template for adding peer using enums
     * @internal
     */
    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t add(T1 id, const uint8_t *mac, uint8_t channel, T2 type, uint32_t heartbeat_interval_ms = 0)
    {
        return add(static_cast<NodeId>(id), mac, channel, static_cast<NodeType>(type), heartbeat_interval_ms);
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
    virtual std::vector<PeerInfo> get_all() = 0;

    /**
     * @brief Get peers that haven't been seen since a timeout
     * @internal
     */
    virtual std::vector<NodeId> get_offline(uint64_t now_ms) = 0;

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
     * @internal
     */
    virtual esp_err_t load_from_storage(uint8_t &wifi_channel) = 0;

    /**
     * @brief Persist peer list to storage
     * @internal
     */
    virtual void persist(uint8_t wifi_channel) = 0;
};

/**
 * @interface ITxStateMachine
 * @brief State machine for managing transmission lifecycle and retries (internal)
 * @internal
 */
class ITxStateMachine
{
public:
    virtual ~ITxStateMachine() = default;

    /** @internal */
    virtual TxState on_tx_success(bool requires_ack) = 0;
    /** @internal */
    virtual TxState on_ack_received() = 0;
    /** @internal */
    virtual TxState on_ack_timeout() = 0;
    /** @internal */
    virtual TxState on_physical_fail() = 0;
    /** @internal */
    virtual TxState on_max_retries() = 0;
    /** @internal */
    virtual void on_link_alive() = 0;
    /** @internal */
    virtual TxState get_state() const = 0;
    /** @internal */
    virtual void reset() = 0;
    /** @internal */
    virtual void set_pending_ack(const PendingAck &pending_ack) = 0;
    /** @internal */
    virtual std::optional<PendingAck> get_pending_ack() const = 0;
};

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
    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    void update_node_info(T1 id, T2 type)
    {
        update_node_info(static_cast<NodeId>(id), static_cast<NodeType>(type));
    }
};

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

/**
 * @interface IPersistenceBackend
 * @brief Low-level storage backend (NVS/RTC) (internal)
 * @internal
 */
class IPersistenceBackend
{
public:
    virtual ~IPersistenceBackend() = default;

    /** @internal */
    virtual esp_err_t load(void *data, size_t size) = 0;
    /** @internal */
    virtual esp_err_t save(const void *data, size_t size) = 0;
};

/**
 * @interface IStorage
 * @brief Higher-level storage management for peers and config (internal)
 * @internal
 */
class IStorage
{
public:
    virtual ~IStorage() = default;

    /** @internal */
    virtual esp_err_t load(uint8_t &wifi_channel, std::vector<PersistentPeer> &peers) = 0;
    /** @internal */
    virtual esp_err_t save(uint8_t wifi_channel,
                           const std::vector<PersistentPeer> &peers,
                           bool force_nvs_commit = true) = 0;
};

/**
 * @interface IWiFiHAL
 * @brief Hardware Abstraction Layer for WiFi and ESP-NOW drivers (internal)
 * @internal
 */
class IWiFiHAL
{
public:
    virtual ~IWiFiHAL() = default;

    /** @internal */
    virtual esp_err_t set_channel(uint8_t channel) = 0;
    /** @internal */
    virtual esp_err_t get_channel(uint8_t *channel) = 0;
    /** @internal */
    virtual esp_err_t send_packet(const uint8_t *mac, const uint8_t *data, size_t len) = 0;
    /** @internal */
    virtual bool wait_for_event(uint32_t event_mask, uint32_t timeout_ms) = 0;
    /** @internal */
    virtual void set_task_to_notify(TaskHandle_t task_handle) = 0;
};

/**
 * @interface ITxManager
 * @brief Manager for transmission queue and background sending task (internal)
 * @internal
 */
class ITxManager
{
public:
    virtual ~ITxManager() = default;

    /** @internal */
    virtual esp_err_t init(uint32_t stack_size, UBaseType_t priority) = 0;
    /** @internal */
    virtual esp_err_t deinit() = 0;
    /** @internal */
    virtual esp_err_t queue_packet(const TxPacket &packet) = 0;
    /** @internal */
    virtual void notify_physical_fail() = 0;
    /** @internal */
    virtual void notify_link_alive() = 0;
    /** @internal */
    virtual void notify_logical_ack() = 0;
    /** @internal */
    virtual void notify_hub_found() = 0;
    /** @internal */
    virtual TaskHandle_t get_task_handle() const = 0;
};

/**
 * @interface IHeartbeatManager
 * @brief Heartbeat generation and monitoring (internal)
 * @internal
 */
class IHeartbeatManager
{
public:
    virtual ~IHeartbeatManager() = default;

    /** @internal */
    virtual esp_err_t init(uint32_t interval_ms, NodeType type) = 0;
    /** @internal */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeType)>>
    esp_err_t init(uint32_t interval_ms, T type)
    {
        return init(interval_ms, static_cast<NodeType>(type));
    }

    /** @internal */
    virtual void update_node_id(NodeId id) = 0;
    /** @internal */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void update_node_id(T id)
    {
        update_node_id(static_cast<NodeId>(id));
    }

    /** @internal */
    virtual esp_err_t deinit() = 0;
    /** @internal */
    virtual void handle_response(NodeId hub_id, uint8_t channel) = 0;
    /** @internal */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void handle_response(T hub_id, uint8_t channel)
    {
        handle_response(static_cast<NodeId>(hub_id), channel);
    }

    /** @internal */
    virtual void handle_request(NodeId sender_id, const uint8_t *mac, uint64_t uptime_ms) = 0;
    /** @internal */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void handle_request(T sender_id, const uint8_t *mac, uint64_t uptime_ms)
    {
        handle_request(static_cast<NodeId>(sender_id), mac, uptime_ms);
    }
};

/**
 * @interface IPairingManager
 * @brief Pairing logic for connecting nodes to HUB (internal)
 * @internal
 */
class IPairingManager
{
public:
    virtual ~IPairingManager() = default;

    /** @internal */
    virtual esp_err_t init(NodeType type, NodeId id) = 0;
    /** @internal */
    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeType)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeId)>>
    esp_err_t init(T1 type, T2 id)
    {
        return init(static_cast<NodeType>(type), static_cast<NodeId>(id));
    }

    /** @internal */
    virtual esp_err_t deinit() = 0;
    /** @internal */
    virtual esp_err_t start(uint32_t timeout_ms) = 0;
    /** @internal */
    virtual bool is_active() const = 0;
    /** @internal */
    virtual void handle_request(const RxPacket &packet) = 0;
    /** @internal */
    virtual void handle_response(const RxPacket &packet) = 0;
};

/**
 * @interface IMessageRouter
 * @brief Routing of received packets to appropriate managers or app queue (internal)
 * @internal
 */
class IMessageRouter
{
public:
    virtual ~IMessageRouter() = default;

    /** @internal */
    virtual void handle_packet(const RxPacket &packet) = 0;
    /** @internal */
    virtual bool should_dispatch_to_worker(MessageType type) = 0;
    /** @internal */
    virtual void set_app_queue(QueueHandle_t app_queue) = 0;
    /** @internal */
    virtual void set_node_info(NodeId id, NodeType type) = 0;

    /** @internal */
    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    void set_node_info(T1 id, T2 type)
    {
        set_node_info(static_cast<NodeId>(id), static_cast<NodeType>(type));
    }
};
