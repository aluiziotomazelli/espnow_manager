#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_storage_manager.hpp"
#include "mock_hal_freertos.hpp"

#include "statistics_manager.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

// ==========================================================================
// Test Fixture for StatisticsManager
// ==========================================================================
class StatisticsManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockStorageManager> storage_manager;
    NiceMock<MockFreeRTOSHAL> hal_freertos;
    std::unique_ptr<StatisticsManager> sut;

    void SetUp() override
    {
        // Setup mock to return a valid handle for mutex
        ON_CALL(hal_freertos, mutex_create()).WillByDefault(Return((SemaphoreHandle_t)0x1234));
        ON_CALL(hal_freertos, semaphore_take(_, _)).WillByDefault(Return(pdTRUE));

        sut = std::make_unique<StatisticsManager>(storage_manager, hal_freertos);
        sut->init();
    }
};

TEST_F(StatisticsManagerTest, PeerAddedAppearsInGetAll)
{
    // Arrange
    NodeId node_id = 10;
    uint32_t heartbeat_interval = 1000;

    // Act
    sut->on_peer_added(node_id, heartbeat_interval);
    auto all_stats = sut->get_all();

    // Assert
    EXPECT_EQ(all_stats.size(), 1);
    EXPECT_EQ(all_stats[0].node_id, node_id);
}

TEST_F(StatisticsManagerTest, PacketReceivedUpdatesRssiAvg)
{
    // Arrange
    NodeId node_id = 10;
    int8_t initial_rssi = -60;
    int8_t second_rssi = -70;

    sut->on_peer_added(node_id, 1000); // interval 1s -> alpha 26 (10%)

    // Act
    sut->on_packet_received(node_id, initial_rssi);
    sut->on_packet_received(node_id, second_rssi);

    // Assert
    auto all_stats = sut->get_all();
    EXPECT_NE(all_stats[0].rssi_avg, initial_rssi);
    EXPECT_NE(all_stats[0].rssi_avg, second_rssi);
}

TEST_F(StatisticsManagerTest, AckReceivedUpdatesRttAvg)
{
    // Arrange
    NodeId node_id = 10;
    uint32_t rtt1 = 20;
    uint32_t rtt2 = 40;

    sut->on_peer_added(node_id, 1000);

    // Act
    sut->on_ack_received(node_id, rtt1);
    sut->on_ack_received(node_id, rtt2);

    // Assert
    auto all_stats = sut->get_all();
    EXPECT_GT(all_stats[0].rtt_avg_ms, rtt1);
    EXPECT_LT(all_stats[0].rtt_avg_ms, rtt2);
}

// Flush thresholds
TEST_F(StatisticsManagerTest, ReachingThresholdRxTriggersFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    // FLUSH_THRESHOLD_RX = 50, deve flushear uma vez
    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);

    for (int i = 0; i < FLUSH_THRESHOLD_RX; ++i) {
        sut->on_packet_received(node_id, -60);
    }
}

// Delivery success/failure/driver_error do NOT have dirty counters — they update
// stats directly. Flush is triggered by the next on_packet_received or
// on_ack_received call, or by deinit().
TEST_F(StatisticsManagerTest, DeliverySuccessUpdatesPacketsSent)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    sut->on_delivery_success(node_id);

    PeerStatistics stats;
    EXPECT_TRUE(sut->get(node_id, stats));
    EXPECT_EQ(stats.packets_sent, 1);
}

TEST_F(StatisticsManagerTest, ReachingThresholdDeliverySuccessTriggersFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    // FLUSH_THRESHOLD_TX = 50
    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);

    for (int i = 0; i < FLUSH_THRESHOLD_TX; ++i) {
        sut->on_delivery_success(node_id);
    }
}

TEST_F(StatisticsManagerTest, DeliveryFailureUpdatesDeliveryFailures)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    sut->on_delivery_failure(node_id);

    PeerStatistics stats;
    EXPECT_TRUE(sut->get(node_id, stats));
    EXPECT_EQ(stats.delivery_failures, 1);
}

TEST_F(StatisticsManagerTest, DriverErrorIncrementsPerPeerCounter)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    sut->on_driver_error(node_id);
    sut->on_driver_error(node_id);

    PeerStatistics stats;
    EXPECT_TRUE(sut->get(node_id, stats));
    EXPECT_EQ(stats.driver_errors, 2);
}

TEST_F(StatisticsManagerTest, ReachingThresholdDriverErrorTriggersFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);

    for (int i = 0; i < FLUSH_THRESHOLD_TX_FAILURE; ++i) {
        sut->on_driver_error(node_id);
    }
}

TEST_F(StatisticsManagerTest, ReachingThresholdRttTriggersFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    // FLUSH_THRESHOLD_RTT = 30, deve flushear uma vez
    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);

    for (int i = 0; i < FLUSH_THRESHOLD_RTT; ++i) {
        sut->on_ack_received(node_id, 20);
    }
}

TEST_F(StatisticsManagerTest, ReachingThresholdDeliveryFailureTriggersFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    // FLUSH_THRESHOLD_TX_FAILURE = 10
    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);

    for (int i = 0; i < FLUSH_THRESHOLD_TX_FAILURE; ++i) {
        sut->on_delivery_failure(node_id);
    }
}

TEST_F(StatisticsManagerTest, ReachingThresholdRetryTriggersFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    // on_retry increments dirty_tx (soft), threshold is FLUSH_THRESHOLD_TX = 50
    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);

    for (int i = 0; i < FLUSH_THRESHOLD_TX; ++i) {
        sut->on_retry(node_id);
    }
}

TEST_F(StatisticsManagerTest, ReachingThresholdLostTriggersFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);

    for (int i = 0; i < FLUSH_THRESHOLD_LOSS; ++i) {
        sut->on_packet_lost(node_id);
    }
}

TEST_F(StatisticsManagerTest, DeliveryFailureIncrementsPerPeerCounter)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    sut->on_delivery_failure(node_id);
    sut->on_delivery_failure(node_id);

    PeerStatistics stats;
    EXPECT_TRUE(sut->get(node_id, stats));
    EXPECT_EQ(stats.delivery_failures, 2);
}

TEST_F(StatisticsManagerTest, ChangingHeartbeatUpdatesAlpha)
{
    NodeId node_id = 10;

    // Adiciona com heartbeat rápido (1s -> alpha 26)
    sut->on_peer_added(node_id, 1000);
    PeerStatistics stats;
    sut->get(node_id, stats);
    uint8_t alpha1 = stats.rssi_alpha;

    // Change to slow heartbeat (29s -> alpha 64)
    sut->on_peer_added(node_id, 29000);
    sut->get(node_id, stats);
    uint8_t alpha2 = stats.rssi_alpha;

    EXPECT_NE(alpha1, alpha2);
    EXPECT_GT(alpha2, alpha1);
}

TEST_F(StatisticsManagerTest, DeinitTriggersFinalFlush)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);
    sut->on_packet_received(node_id, -60); // dirty_rx = 1

    EXPECT_CALL(storage_manager, store_stats(_)).Times(1);
    sut->deinit();
}

TEST_F(StatisticsManagerTest, PeerRemovedClearsStatistics)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);
    sut->on_peer_removed(node_id);

    auto all_stats = sut->get_all();
    EXPECT_EQ(all_stats.size(), 0);
}

TEST_F(StatisticsManagerTest, RetryUpdatesCounters)
{
    NodeId node_id = 10;
    sut->on_peer_added(node_id, 1000);

    sut->on_retry(node_id);

    PeerStatistics stats;
    EXPECT_TRUE(sut->get(node_id, stats));
    EXPECT_EQ(stats.retries, 1);
}

TEST_F(StatisticsManagerTest, UnregisteredPeerOperationsDoNotCrash)
{
    NodeId unknown_node = 99;

    // Should not crash or call storage
    EXPECT_CALL(storage_manager, store_stats(_)).Times(0);

    sut->on_packet_received(unknown_node, -60);
    sut->on_ack_received(unknown_node, 20);
    sut->on_delivery_failure(unknown_node);
}

TEST_F(StatisticsManagerTest, MultiplePeersMaintainIndependentStats)
{
    NodeId node1 = 10;
    NodeId node2 = 20;

    sut->on_peer_added(node1, 1000);
    sut->on_peer_added(node2, 1000);

    // Node 1: Receives packets
    sut->on_packet_received(node1, -60);

    // Node 2: Receives ACKs
    sut->on_ack_received(node2, 30);

    PeerStatistics stats1, stats2;
    EXPECT_TRUE(sut->get(node1, stats1));
    EXPECT_TRUE(sut->get(node2, stats2));

    // Independent validations
    EXPECT_EQ(stats1.packets_rx, 1);
    EXPECT_EQ(stats1.packets_sent, 0);
    EXPECT_EQ(stats1.rtt_avg_ms, 0);

    EXPECT_EQ(stats2.packets_rx, 0);
    EXPECT_EQ(stats2.rtt_avg_ms, 30);
}

TEST_F(StatisticsManagerTest, InitLoadsPersistedStats)
{
    // Arrange: Simulates a pre-existing persistence
    NodeId node_id = 42;
    PeerStatisticsPersist persisted;
    persisted.node_id = node_id;
    persisted.rssi_avg = -55;
    persisted.packets_rx = 50;
    persisted.packets_sent = 40;
    persisted.packets_lost = 2;
    persisted.rtt_avg_ms = 15;

    etl::vector<PeerStatisticsPersist, MAX_PEERS> persisted_list;
    persisted_list.push_back(persisted);

    // Configure the mock to return the persisted data
    EXPECT_CALL(storage_manager, load_stats(_))
        .WillOnce(Invoke([&persisted_list](etl::ivector<PeerStatisticsPersist>& stats) {
            stats.clear();
            for (const auto& p : persisted_list) {
                stats.push_back(p);
            }
            return ESP_OK;
        }));

    // Act: Re-initialize the SUT to force loading
    sut = std::make_unique<StatisticsManager>(storage_manager, hal_freertos);
    sut->init();

    // Assert
    PeerStatistics stats;
    EXPECT_TRUE(sut->get(node_id, stats));
    EXPECT_EQ(stats.node_id, node_id);
    EXPECT_EQ(stats.rssi_avg, -55);
    EXPECT_EQ(stats.packets_rx, 50);
    EXPECT_EQ(stats.rtt_avg_ms, 15);
}