#pragma once

#include "espnow_types.hpp"

namespace field_test {

static constexpr espnow::NodeId NODE_ID = 10;
static constexpr espnow::NodeType NODE_TYPE = 0x04;
static constexpr espnow::PayloadType TEST_PAYLOAD_TYPE = 0x01;
static constexpr uint32_t SEND_INTERVAL_MS = 1000;

} // namespace field_test
