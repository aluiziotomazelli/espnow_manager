#include "espnow_manager.hpp"
#include "host_test_common.hpp"
#include "unity.h"
#include <memory>

TEST_CASE("EspNow can be instantiated with mocks", "[espnow]")
{
    auto pm = std::make_unique<MockPeerManager>();
    auto tx = std::make_unique<MockTxStateMachine>();
    auto cs = std::make_unique<MockChannelScanner>();
    auto mc = std::make_unique<MockMessageCodec>();

    EspNow espnow(std::move(pm), std::move(tx), std::move(cs), std::move(mc));

    TEST_ASSERT_TRUE(true);
}
