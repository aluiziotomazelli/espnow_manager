// mock_message_router.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_message_router.hpp"

namespace espnow {

class MockMessageRouter : public IMessageRouter
{
public:
    MOCK_METHOD(void, handle_packet, (const DecodedRxPacket& decoded), (override));
};

} // namespace espnow
