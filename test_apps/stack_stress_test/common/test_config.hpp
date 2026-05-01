#pragma once

#include <cstdint>

namespace stack_test {

constexpr uint32_t SEND_INTERVAL_MS = 250;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = SEND_INTERVAL_MS * 4;
constexpr uint32_t CHANNEL_MONITOR_INTERVAL_MS = 100;

constexpr espnow::NodeId NODE_ID = 0x02;
constexpr espnow::NodeType NODE_TYPE = 0x02;
static constexpr espnow::PayloadType TEST_PAYLOAD_TYPE = 0x01;

struct AppMessage
{
    uint8_t dummy_data[32]; // Just some payload to take up queue space
    bool requires_ack;
    uint8_t sender_id;
    uint32_t sequence_number;
};

} // namespace stack_test
