#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <vector>

#include "storage_manager.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class MockPersistenceBackend : public IPersistenceBackend
{
public:
    MOCK_METHOD(esp_err_t, load, (void* data, size_t size), (override));
    MOCK_METHOD(esp_err_t, save, (const void* data, size_t size), (override));
};

class StorageManagerTest : public ::testing::Test
{
protected:
    MockPersistenceBackend* rtc_peer_mock;
    MockPersistenceBackend* rtc_channel_mock;
    MockPersistenceBackend* rtc_stats_mock;
    MockPersistenceBackend* nvs_peer_mock;
    MockPersistenceBackend* nvs_channel_mock;
    MockPersistenceBackend* nvs_stats_mock;
    std::unique_ptr<StorageManager> manager;

    uint8_t channel = 0;
    etl::vector<PersistentPeer, MAX_PEERS> peers;
    etl::vector<PeerStatisticsPersist, MAX_PEERS> stats;

    void SetUp() override
    {
        auto rtc_peers = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto rtc_channel = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto rtc_stats = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto nvs_peers = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto nvs_channel = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto nvs_stats = std::make_unique<NiceMock<MockPersistenceBackend>>();

        rtc_peer_mock = rtc_peers.get();
        rtc_channel_mock = rtc_channel.get();
        rtc_stats_mock = rtc_stats.get();
        nvs_peer_mock = nvs_peers.get();
        nvs_channel_mock = nvs_channel.get();
        nvs_stats_mock = nvs_stats.get();

        manager = std::make_unique<StorageManager>(
            std::move(rtc_peers),
            std::move(rtc_channel),
            std::move(rtc_stats),
            std::move(nvs_peers),
            std::move(nvs_channel),
            std::move(nvs_stats));
    }
};

// Helper to generate a list of dummy peers for testing.
static etl::vector<PersistentPeer, MAX_PEERS * 2> create_test_peers(int count)
{
    etl::vector<PersistentPeer, MAX_PEERS * 2> peers;
    for (int i = 0; i < count; ++i) {
        PersistentPeer p;
        memset(&p, 0, sizeof(p));
        p.node_id = (uint8_t)(i + 10);
        p.type = 2; // SENSOR
        memset(p.mac, i, 6);
        peers.push_back(p);
    }
    return peers;
}

static PersistentPeers create_valid_persistent_peers(uint8_t num_peers)
{
    PersistentPeers valid_data = {};
    valid_data.magic = PersistentPeers::MAGIC;
    valid_data.version = PersistentPeers::VERSION;
    valid_data.num_peers = num_peers;
    valid_data.crc = StorageManager::calculate_crc(valid_data);
    return valid_data;
}

static PersistentChannel create_valid_persistent_channel(uint8_t channel)
{
    PersistentChannel valid_data = {};
    valid_data.magic = PersistentChannel::MAGIC;
    valid_data.wifi_channel = channel;
    valid_data.crc = StorageManager::calculate_crc(valid_data);
    return valid_data;
}

static etl::vector<PeerStatisticsPersist, MAX_PEERS * 2> create_test_stats(int count)
{
    etl::vector<PeerStatisticsPersist, MAX_PEERS * 2> stats;
    for (int i = 0; i < count; ++i) {
        PeerStatisticsPersist s;
        s.node_id = (uint8_t)(i + 10);
        s.rssi_avg = -60 - i;
        s.packets_rx = 100 + i;
        s.packets_tx = 90 + i;
        s.packets_lost = 5 + i;
        s.rtt_avg_ms = 10 + i;
        stats.push_back(s);
    }
    return stats;
}

static PersistentStats create_valid_persistent_stats(uint8_t num_stats)
{
    PersistentStats valid_data = {};
    valid_data.magic = PersistentStats::MAGIC;
    valid_data.version = PersistentStats::VERSION;
    valid_data.num_stats = num_stats;
    valid_data.crc = StorageManager::calculate_crc(valid_data);
    return valid_data;
}

// -------------------------------------------------------------------
// Load channel
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, ChannelLoadingFromRtcWhenValid)
{
    auto valid_data = create_valid_persistent_channel(6);
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).Times(0);
    EXPECT_EQ(ESP_OK, manager->load_channel(channel));
    EXPECT_EQ(6, channel);
}

TEST_F(StorageManagerTest, ChannelLoadingFromNvsWhenRtcFails)
{
    auto valid_data = create_valid_persistent_channel(6);
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_EQ(ESP_OK, manager->load_channel(channel));
    EXPECT_EQ(6, channel);
}

TEST_F(StorageManagerTest, ChannelLoadingRtcAndNvsFailReturnsError)
{
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_channel(channel));
}

TEST_F(StorageManagerTest, ChannelLoadingInvalidMagicReturnsError)
{
    auto valid_data = create_valid_persistent_channel(6);
    valid_data.magic = 0xDEADBEEF;
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_channel(channel));
}

TEST_F(StorageManagerTest, ChannelLoadingInvalidCrcReturnsError)
{
    auto valid_data = create_valid_persistent_channel(6);
    valid_data.crc = 0xDEADBEEF;
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_channel(channel));
}

TEST_F(StorageManagerTest, ChannelLoadingFromNvsWithChannelSyncsRtc)
{
    auto valid_data = create_valid_persistent_channel(6);
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_channel_mock, save(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(ESP_OK, manager->load_channel(channel));
}

// -------------------------------------------------------------------
// Store channel
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, SaveDifferentChannelSavesToRtcAndNvs)
{
    EXPECT_CALL(*rtc_channel_mock, save(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(*nvs_channel_mock, save(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(ESP_OK, manager->store_channel(6));
}

TEST_F(StorageManagerTest, SaveSameChannelDoesNotSave)
{
    auto valid_data = create_valid_persistent_channel(6);
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_channel_mock, save(_, _)).Times(0);
    EXPECT_CALL(*nvs_channel_mock, save(_, _)).Times(0);
    EXPECT_EQ(ESP_OK, manager->store_channel(6));
}

TEST_F(StorageManagerTest, SaveSameChannelButRtcFailsToLoadCallsSave)
{
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*rtc_channel_mock, save(_, _)).Times(1);
    EXPECT_CALL(*nvs_channel_mock, save(_, _)).Times(1);
    EXPECT_EQ(ESP_OK, manager->store_channel(6));
}

TEST_F(StorageManagerTest, SaveToNvsFailsOnSaveChannelReturnsError)
{
    EXPECT_CALL(*rtc_channel_mock, save(_, _)).Times(1);
    EXPECT_CALL(*nvs_channel_mock, save(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->store_channel(6));
}

// -------------------------------------------------------------------
// Load peers
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, LoadPeersFromRtcWhenValid)
{
    auto valid_data = create_valid_persistent_peers(6);
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_EQ(ESP_OK, manager->load_peers(peers));
    EXPECT_EQ(6, peers.size());
}

TEST_F(StorageManagerTest, LoadPeersFromNvsWhenRtcFails)
{
    auto valid_data = create_valid_persistent_peers(6);
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_EQ(ESP_OK, manager->load_peers(peers));
    EXPECT_EQ(6, peers.size());
}

TEST_F(StorageManagerTest, LoadPeersRtcAndNvsFailReturnsError)
{
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_peers(peers));
}

TEST_F(StorageManagerTest, LoadPeersInvalidMagicReturnsError)
{
    auto valid_data = create_valid_persistent_peers(6);
    valid_data.magic = 0xDEADBEEF;
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_peers(peers));
}

TEST_F(StorageManagerTest, LoadPeersInvalidVersionReturnsError)
{
    auto valid_data = create_valid_persistent_peers(6);
    valid_data.version = 0xDEAD;
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_peers(peers));
}

TEST_F(StorageManagerTest, LoadPeersInvalidCrcReturnsError)
{
    auto valid_data = create_valid_persistent_peers(6);
    valid_data.crc = 0xDEADBEEF;
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_peers(peers));
}

TEST_F(StorageManagerTest, LoadTruncatesWhenDataExceedsVectorCapacity)
{
    auto oversized_peers_data = create_valid_persistent_peers(MAX_PEERS + 6);
    for (int i = 0; i < MAX_PEERS; ++i) memset(oversized_peers_data.peers[i].mac, i, 6);
    oversized_peers_data.crc = manager->calculate_crc(oversized_peers_data);

    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &oversized_peers_data, size);
        return ESP_OK;
    });

    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_OK, manager->load_peers(loaded_peers));
    EXPECT_EQ(MAX_PEERS, loaded_peers.size());
}

TEST_F(StorageManagerTest, LoadPeersFromNvsWithPeersSyncsRtc)
{
    auto test_peers = create_test_peers(1);
    auto peers_data = create_valid_persistent_peers(1);
    peers_data.peers[0] = test_peers[0];
    peers_data.crc = manager->calculate_crc(peers_data);

    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    ON_CALL(*nvs_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &peers_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(1);
    EXPECT_EQ(ESP_OK, manager->load_peers(peers));
}

// -------------------------------------------------------------------
// Store peers
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, SaveNewPeerSavesToRtcAndNvs)
{
    auto test_peers = create_test_peers(1);
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(*nvs_peer_mock, save(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(ESP_OK, manager->store_peers(test_peers, false));
}

TEST_F(StorageManagerTest, SaveSamePeersDoesNotSave)
{
    auto test_peers = create_test_peers(1);
    auto peers_data = create_valid_persistent_peers(1);
    peers_data.peers[0] = test_peers[0];
    peers_data.crc = StorageManager::calculate_crc(peers_data);

    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &peers_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(0);
    EXPECT_CALL(*nvs_peer_mock, save(_, _)).Times(0);
    EXPECT_EQ(ESP_OK, manager->store_peers(test_peers, false));
}

TEST_F(StorageManagerTest, SaveSamePeersWithForceNvsCommitSavesToNvs)
{
    auto test_peers = create_test_peers(1);
    auto peers_data = create_valid_persistent_peers(1);
    peers_data.peers[0] = test_peers[0];
    peers_data.crc = StorageManager::calculate_crc(peers_data);

    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &peers_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(0);
    EXPECT_CALL(*nvs_peer_mock, save(_, _)).Times(1);
    EXPECT_EQ(ESP_OK, manager->store_peers(test_peers, true));
}

TEST_F(StorageManagerTest, SavePeersWithMorePeersThanMaxTruncatesPeersAtMax)
{
    auto test_peers = create_test_peers(MAX_PEERS + 1);
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).WillOnce([&](const void* data, size_t size) {
        const PersistentPeers* saved = static_cast<const PersistentPeers*>(data);
        EXPECT_EQ(MAX_PEERS, saved->num_peers);
        return ESP_OK;
    });
    EXPECT_EQ(ESP_OK, manager->store_peers(test_peers, true));
}

TEST_F(StorageManagerTest, SaveSamePeersButRtcFailsToLoadCallsSave)
{
    auto test_peers = create_test_peers(1);
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(1);
    EXPECT_CALL(*nvs_peer_mock, save(_, _)).Times(1);
    EXPECT_EQ(ESP_OK, manager->store_peers(test_peers, false));
}

// -------------------------------------------------------------------
// Load statistics
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, LoadStatsFromRtcWhenValid)
{
    auto valid_data = create_valid_persistent_stats(3);
    auto test_stats = create_test_stats(3);
    for (int i = 0; i < 3; ++i) valid_data.stats[i] = test_stats[i];
    valid_data.crc = StorageManager::calculate_crc(valid_data);

    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_stats_mock, load(_, _)).Times(0);

    EXPECT_EQ(ESP_OK, manager->load_stats(stats));
    EXPECT_EQ(3, stats.size());
    EXPECT_EQ(test_stats[0].node_id, stats[0].node_id);
}

TEST_F(StorageManagerTest, LoadStatsFromNvsWhenRtcFails)
{
    auto valid_data = create_valid_persistent_stats(3);
    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_stats_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_EQ(ESP_OK, manager->load_stats(stats));
    EXPECT_EQ(3, stats.size());
}

TEST_F(StorageManagerTest, LoadStatsRtcAndNvsFailReturnsError)
{
    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_stats_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_stats(stats));
}

TEST_F(StorageManagerTest, LoadStatsInvalidMagicReturnsError)
{
    auto valid_data = create_valid_persistent_stats(3);
    valid_data.magic = 0xDEADBEEF;
    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_stats_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_stats(stats));
}

TEST_F(StorageManagerTest, LoadStatsInvalidCrcReturnsError)
{
    auto valid_data = create_valid_persistent_stats(3);
    valid_data.crc = 0xDEADBEEF;
    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*nvs_stats_mock, load(_, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_NE(ESP_OK, manager->load_stats(stats));
}

TEST_F(StorageManagerTest, LoadStatsFromNvsWithStatsSyncsRtc)
{
    auto test_stats = create_test_stats(1);
    auto stats_data = create_valid_persistent_stats(1);
    stats_data.stats[0] = test_stats[0];
    stats_data.crc = StorageManager::calculate_crc(stats_data);

    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    ON_CALL(*nvs_stats_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &stats_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_stats_mock, save(_, _)).Times(1);
    EXPECT_EQ(ESP_OK, manager->load_stats(stats));
}

// -------------------------------------------------------------------
// Store statistics
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, SaveNewStatsSavesToRtcAndNvs)
{
    auto test_stats = create_test_stats(2);
    EXPECT_CALL(*rtc_stats_mock, save(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(*nvs_stats_mock, save(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(ESP_OK, manager->store_stats(test_stats));
}

TEST_F(StorageManagerTest, SaveSameStatsDoesNotSave)
{
    auto test_stats = create_test_stats(1);
    auto stats_data = create_valid_persistent_stats(1);
    stats_data.stats[0] = test_stats[0];
    stats_data.crc = StorageManager::calculate_crc(stats_data);

    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &stats_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_stats_mock, save(_, _)).Times(0);
    EXPECT_CALL(*nvs_stats_mock, save(_, _)).Times(0);
    EXPECT_EQ(ESP_OK, manager->store_stats(test_stats));
}

TEST_F(StorageManagerTest, SaveSameStatsButRtcFailsToLoadCallsSave)
{
    auto test_stats = create_test_stats(1);
    ON_CALL(*rtc_stats_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(*rtc_stats_mock, save(_, _)).Times(1);
    EXPECT_CALL(*nvs_stats_mock, save(_, _)).Times(1);
    EXPECT_EQ(ESP_OK, manager->store_stats(test_stats));
}

TEST_F(StorageManagerTest, SaveStatsTruncatesWhenExceedingMax)
{
    auto oversized_stats = create_test_stats(MAX_PEERS + 1);
    EXPECT_CALL(*rtc_stats_mock, save(_, _)).WillOnce([&](const void* data, size_t size) {
        const PersistentStats* saved = static_cast<const PersistentStats*>(data);
        EXPECT_EQ(MAX_PEERS, saved->num_stats);
        return ESP_OK;
    });
    EXPECT_EQ(ESP_OK, manager->store_stats(oversized_stats));
}
