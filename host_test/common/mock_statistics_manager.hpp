// host_test/common/mock_statistics_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_statistics_manager.hpp"

namespace espnow {

class MockStatisticsManager : public IStatisticsManager
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));

    MOCK_METHOD(void, on_peer_added, (NodeId node_id, uint32_t heartbeat_interval_ms), (override));
    MOCK_METHOD(void, on_peer_removed, (NodeId node_id), (override));

    MOCK_METHOD(void, on_packet_received, (NodeId node_id, int8_t rssi), (override));
    MOCK_METHOD(void, on_ack_received, (NodeId node_id, uint32_t rtt_ms), (override));

    MOCK_METHOD(void, on_delivery_success, (NodeId node_id), (override));
    MOCK_METHOD(void, on_delivery_failure, (NodeId node_id), (override));
    MOCK_METHOD(void, on_driver_error, (NodeId node_id), (override));
    MOCK_METHOD(void, on_packet_lost, (NodeId node_id), (override));
    MOCK_METHOD(void, on_retry, (NodeId node_id), (override));

    MOCK_METHOD(void, sync_peers, (const etl::ivector<PeerInfo>& known_peers), (override));

    MOCK_METHOD(bool, get, (NodeId node_id, PeerStatistics& out), (const, override));
    MOCK_METHOD((etl::vector<PeerStatistics, MAX_PEERS>), get_all, (), (const, override));
};

} // namespace espnow
