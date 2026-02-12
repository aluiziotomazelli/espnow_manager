#include "espnow_manager.hpp"
#include "host_test_common.hpp"
#include "unity.h"
#include <memory>

#include "mock_storage.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_message_codec.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_channel_scanner.hpp"

TEST_CASE("EspNow can be instantiated with mocks", "[espnow]")
{
    auto pm = std::make_unique<MockPeerManager>();
    auto tx = std::make_unique<MockTxManager>();
    auto cs = std::make_unique<MockChannelScanner>();
    auto mc = std::make_unique<MockMessageCodec>();
    auto hm = std::make_unique<MockHeartbeatManager>();
    auto pam = std::make_unique<MockPairingManager>();

    EspNow espnow(std::move(pm), std::move(tx), cs.get(), std::move(mc), std::move(hm), std::move(pam));

    TEST_ASSERT_TRUE(true);
}
