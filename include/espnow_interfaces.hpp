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

class IPeerManager
{
public:
    virtual ~IPeerManager()                                   = default;
    virtual esp_err_t add(NodeId id,
                          const uint8_t *mac,
                          uint8_t channel,
                          NodeType type,
                          uint32_t heartbeat_interval_ms = 0) = 0;

    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t add(T1 id, const uint8_t *mac, uint8_t channel, T2 type, uint32_t heartbeat_interval_ms = 0)
    {
        return add(static_cast<NodeId>(id), mac, channel, static_cast<NodeType>(type), heartbeat_interval_ms);
    }

    virtual esp_err_t remove(NodeId id) = 0;
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t remove(T id)
    {
        return remove(static_cast<NodeId>(id));
    }

    virtual bool find_mac(NodeId id, uint8_t *mac) = 0;
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    bool find_mac(T id, uint8_t *mac)
    {
        return find_mac(static_cast<NodeId>(id), mac);
    }

    virtual std::vector<PeerInfo> get_all()                  = 0;
    virtual std::vector<NodeId> get_offline(uint64_t now_ms) = 0;

    virtual void update_last_seen(NodeId id, uint64_t now_ms) = 0;
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void update_last_seen(T id, uint64_t now_ms)
    {
        update_last_seen(static_cast<NodeId>(id), now_ms);
    }

    virtual esp_err_t load_from_storage(uint8_t &wifi_channel) = 0;
    virtual void persist(uint8_t wifi_channel)                 = 0;
};

class ITxStateMachine
{
public:
    virtual ~ITxStateMachine()                                  = default;
    virtual TxState on_tx_success(bool requires_ack)            = 0;
    virtual TxState on_ack_received()                           = 0;
    virtual TxState on_ack_timeout()                            = 0;
    virtual TxState on_physical_fail()                          = 0;
    virtual TxState on_max_retries()                            = 0;
    virtual void on_link_alive()                                = 0;
    virtual TxState get_state() const                           = 0;
    virtual void reset()                                        = 0;
    virtual void set_pending_ack(const PendingAck &pending_ack) = 0;
    virtual std::optional<PendingAck> get_pending_ack() const   = 0;
};

class IChannelScanner
{
public:
    virtual ~IChannelScanner() = default;
    struct ScanResult
    {
        uint8_t channel;
        bool hub_found;
    };
    virtual ScanResult scan(uint8_t start_channel)          = 0;
    virtual void update_node_info(NodeId id, NodeType type) = 0;

    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    void update_node_info(T1 id, T2 type)
    {
        update_node_info(static_cast<NodeId>(id), static_cast<NodeType>(type));
    }
};

class IMessageCodec
{
public:
    virtual ~IMessageCodec()                                                                          = default;
    virtual std::vector<uint8_t> encode(const MessageHeader &header, const void *payload, size_t len) = 0;
    virtual std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len)               = 0;
    virtual bool validate_crc(const uint8_t *data, size_t len)                                        = 0;
    virtual uint8_t calculate_crc(const uint8_t *data, size_t len)                                    = 0;
};

class IPersistenceBackend
{
public:
    virtual ~IPersistenceBackend()                        = default;
    virtual esp_err_t load(void *data, size_t size)       = 0;
    virtual esp_err_t save(const void *data, size_t size) = 0;
};

class IStorage
{
public:
    virtual ~IStorage()                                                               = default;
    virtual esp_err_t load(uint8_t &wifi_channel, std::vector<PersistentPeer> &peers) = 0;
    virtual esp_err_t save(uint8_t wifi_channel,
                           const std::vector<PersistentPeer> &peers,
                           bool force_nvs_commit = true)                              = 0;
};

class IWiFiHAL
{
public:
    virtual ~IWiFiHAL()                                                                = default;
    virtual esp_err_t set_channel(uint8_t channel)                                     = 0;
    virtual esp_err_t get_channel(uint8_t *channel)                                    = 0;
    virtual esp_err_t send_packet(const uint8_t *mac, const uint8_t *data, size_t len) = 0;
    virtual bool wait_for_event(uint32_t event_mask, uint32_t timeout_ms)              = 0;
    virtual void set_task_to_notify(TaskHandle_t task_handle)                          = 0;
};

class ITxManager
{
public:
    virtual ~ITxManager()                                             = default;
    virtual esp_err_t init(uint32_t stack_size, UBaseType_t priority) = 0;
    virtual esp_err_t deinit()                                        = 0;
    virtual esp_err_t queue_packet(const TxPacket &packet)            = 0;
    virtual void notify_physical_fail()                               = 0;
    virtual void notify_link_alive()                                  = 0;
    virtual void notify_logical_ack()                                 = 0;
    virtual void notify_hub_found()                                   = 0;
    virtual TaskHandle_t get_task_handle() const                      = 0;
};

class IHeartbeatManager
{
public:
    virtual ~IHeartbeatManager()                                = default;
    virtual esp_err_t init(uint32_t interval_ms, NodeType type) = 0;
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeType)>>
    esp_err_t init(uint32_t interval_ms, T type)
    {
        return init(interval_ms, static_cast<NodeType>(type));
    }

    virtual void update_node_id(NodeId id) = 0;
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void update_node_id(T id)
    {
        update_node_id(static_cast<NodeId>(id));
    }

    virtual esp_err_t deinit()                                   = 0;
    virtual void handle_response(NodeId hub_id, uint8_t channel) = 0;
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void handle_response(T hub_id, uint8_t channel)
    {
        handle_response(static_cast<NodeId>(hub_id), channel);
    }

    virtual void handle_request(NodeId sender_id, const uint8_t *mac, uint64_t uptime_ms) = 0;
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void handle_request(T sender_id, const uint8_t *mac, uint64_t uptime_ms)
    {
        handle_request(static_cast<NodeId>(sender_id), mac, uptime_ms);
    }
};

class IPairingManager
{
public:
    virtual ~IPairingManager()                       = default;
    virtual esp_err_t init(NodeType type, NodeId id) = 0;
    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeType)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeId)>>
    esp_err_t init(T1 type, T2 id)
    {
        return init(static_cast<NodeType>(type), static_cast<NodeId>(id));
    }

    virtual esp_err_t deinit()                           = 0;
    virtual esp_err_t start(uint32_t timeout_ms)         = 0;
    virtual bool is_active() const                       = 0;
    virtual void handle_request(const RxPacket &packet)  = 0;
    virtual void handle_response(const RxPacket &packet) = 0;
};

class IMessageRouter
{
public:
    virtual ~IMessageRouter()                                = default;
    virtual void handle_packet(const RxPacket &packet)       = 0;
    virtual bool should_dispatch_to_worker(MessageType type) = 0;
    virtual void set_app_queue(QueueHandle_t app_queue)      = 0;
    virtual void set_node_info(NodeId id, NodeType type)     = 0;

    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    void set_node_info(T1 id, T2 type)
    {
        set_node_info(static_cast<NodeId>(id), static_cast<NodeType>(type));
    }
};
