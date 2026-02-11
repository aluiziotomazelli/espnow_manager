#pragma once

#include "esp_err.h"
#include "espnow_types.hpp"
#include "protocol_messages.hpp"
#include <optional>
#include <vector>

class IPeerManager
{
public:
    virtual ~IPeerManager() = default;
    virtual esp_err_t add(NodeId id, const uint8_t *mac, uint8_t channel, NodeType type) = 0;
    virtual esp_err_t remove(NodeId id)                                                 = 0;
    virtual bool find_mac(NodeId id, uint8_t *mac)                                      = 0;
    virtual std::vector<PeerInfo> get_all()                                             = 0;
    virtual std::vector<NodeId> get_offline(uint64_t now_ms)                            = 0;
    virtual void update_last_seen(NodeId id, uint64_t now_ms)                           = 0;
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
    virtual ScanResult scan(uint8_t start_channel) = 0;
};

class IMessageCodec
{
public:
    virtual ~IMessageCodec() = default;
    virtual std::vector<uint8_t> encode(const MessageHeader &header,
                                        const void *payload,
                                        size_t len)                             = 0;
    virtual std::optional<MessageHeader> decode_header(const uint8_t *data,
                                                       size_t len)              = 0;
    virtual bool validate_crc(const uint8_t *data, size_t len)                  = 0;
};
