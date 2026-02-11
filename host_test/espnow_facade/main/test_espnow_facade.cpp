#include "espnow_manager.hpp"
#include "unity.h"
#include <memory>

class MockPeerManager : public IPeerManager
{
public:
    esp_err_t add(NodeId id, const uint8_t *mac, uint8_t channel, NodeType type) override { return ESP_OK; }
    esp_err_t remove(NodeId id) override { return ESP_OK; }
    bool find_mac(NodeId id, uint8_t *mac) override { return false; }
    std::vector<PeerInfo> get_all() override { return {}; }
    std::vector<NodeId> get_offline(uint64_t now_ms) override { return {}; }
    void update_last_seen(NodeId id, uint64_t now_ms) override {}
};

class MockTxStateMachine : public ITxStateMachine
{
public:
    TxState on_tx_success(bool requires_ack) override { return TxState::IDLE; }
    TxState on_ack_received() override { return TxState::IDLE; }
    TxState on_ack_timeout() override { return TxState::RETRYING; }
    TxState on_physical_fail() override { return TxState::IDLE; }
    TxState on_max_retries() override { return TxState::IDLE; }
    TxState get_state() const override { return TxState::IDLE; }
    void reset() override {}
    void set_pending_ack(const PendingAck &pending_ack) override {}
    std::optional<PendingAck> get_pending_ack() const override { return std::nullopt; }
};

class MockChannelScanner : public IChannelScanner
{
public:
    ScanResult scan(uint8_t start_channel) override { return {start_channel, false}; }
};

class MockMessageCodec : public IMessageCodec
{
public:
    std::vector<uint8_t> encode(const MessageHeader &header, const void *payload, size_t len) override
    {
        return std::vector<uint8_t>();
    }
    std::optional<MessageHeader> decode_header(const uint8_t *data, size_t len) override { return std::nullopt; }
    bool validate_crc(const uint8_t *data, size_t len) override { return true; }
};

TEST_CASE("EspNow can be instantiated with mocks", "[espnow]")
{
    auto pm = std::make_unique<MockPeerManager>();
    auto tx = std::make_unique<MockTxStateMachine>();
    auto cs = std::make_unique<MockChannelScanner>();
    auto mc = std::make_unique<MockMessageCodec>();

    EspNow espnow(std::move(pm), std::move(tx), std::move(cs), std::move(mc));

    TEST_ASSERT_TRUE(true);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    return UNITY_END();
}
