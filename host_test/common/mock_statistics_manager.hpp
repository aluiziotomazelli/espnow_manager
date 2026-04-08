// host_test/common/mock_statistics_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_statistics_manager.hpp"

class MockStatisticsManager : public IStatisticsManager
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));

    MOCK_METHOD(void, on_peer_added, (NodeId node_id, uint32_t heartbeat_interval_ms), (override));
    MOCK_METHOD(void, on_peer_removed, (NodeId node_id), (override));

    MOCK_METHOD(void, on_packet_received, (NodeId node_id, int8_t rssi, int64_t received_at_ms), (override));
    MOCK_METHOD(void, on_ack_received, (NodeId node_id, uint32_t rtt_ms), (override));

    MOCK_METHOD(void, on_packet_sent, (NodeId node_id, int64_t sent_at_ms), (override));
    MOCK_METHOD(void, on_packet_lost, (NodeId node_id), (override));
    MOCK_METHOD(void, on_transmission_failure, (), (override));
    MOCK_METHOD(void, on_retry, (NodeId node_id), (override));

    MOCK_METHOD(bool, get, (NodeId node_id, PeerStatistics& out), (const, override));
    MOCK_METHOD((etl::vector<PeerStatistics, MAX_PEERS>), get_all, (), (const, override));
};
