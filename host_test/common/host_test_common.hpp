#pragma once

#include "espnow_interfaces.hpp"
#include <memory>
#include <vector>

class MockPeerManager : public IPeerManager
{
public:
    inline esp_err_t add(NodeId id, const uint8_t *mac, uint8_t channel, NodeType type) override
    {
        return ESP_OK;
    }
    inline esp_err_t remove(NodeId id) override
    {
        return ESP_OK;
    }
    inline bool find_mac(NodeId id, uint8_t *mac) override
    {
        return false;
    }
    inline std::vector<PeerInfo> get_all() override
    {
        return {};
    }
    inline std::vector<NodeId> get_offline(uint64_t now_ms) override
    {
        return {};
    }
    inline void update_last_seen(NodeId id, uint64_t now_ms) override
    {
    }
};

class MockTxStateMachine : public ITxStateMachine
{
public:
    inline TxState on_tx_success(bool requires_ack) override
    {
        return TxState::IDLE;
    }
    inline TxState on_ack_received() override
    {
        return TxState::IDLE;
    }
    inline TxState on_ack_timeout() override
    {
        return TxState::RETRYING;
    }
    inline TxState on_physical_fail() override
    {
        return TxState::IDLE;
    }
    inline TxState on_max_retries() override
    {
        return TxState::IDLE;
    }
    inline TxState get_state() const override
    {
        return TxState::IDLE;
    }
    inline void reset() override
    {
    }
    inline void set_pending_ack(const PendingAck &pending_ack) override
    {
    }
    inline std::optional<PendingAck> get_pending_ack() const override
    {
        return std::nullopt;
    }
};

class MockChannelScanner : public IChannelScanner
{
public:
    inline ScanResult scan(uint8_t start_channel) override
    {
        return {start_channel, false};
    }
};

class MockMessageCodec : public IMessageCodec
{
public:
    inline std::vector<uint8_t> encode(const MessageHeader &header, const void *payload, size_t len) override
    {
        return std::vector<uint8_t>();
    }
    inline std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) override
    {
        return std::nullopt;
    }
    inline bool validate_crc(const uint8_t *data, size_t len) override
    {
        return true;
    }
};
