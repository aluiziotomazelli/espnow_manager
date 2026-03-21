#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_discovery_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_message_codec.hpp"

#include "message_router.hpp"

class MessageRouterTest : public ::testing::Test
{
protected:
    NiceMock<MockDiscoveryManager> discovery_manager;
    NiceMock<MockTxManager> tx_manager;
    NiceMock<MockHeartbeatManager> heartbeat_manager;
    NiceMock<MockPairingManager> pairing_manager;
    NiceMock<MockMessageCodec> message_codec;

    MessageRouter router;

    MessageRouterTest()
        : router(discovery_manager, tx_manager, heartbeat_manager, pairing_manager, message_codec)
    {
    }
}