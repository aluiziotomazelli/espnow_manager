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
    MockPersistenceBackend* nvs_peer_mock;
    MockPersistenceBackend* rtc_channel_mock;
    MockPersistenceBackend* nvs_channel_mock;
    std::unique_ptr<StorageManager> manager;

    uint8_t channel = 0;
    etl::vector<PersistentPeer, MAX_PEERS> peers;

    void SetUp() override
    {
        auto rtc_peers = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto nvs_peers = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto rtc_channel = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto nvs_channel = std::make_unique<NiceMock<MockPersistenceBackend>>();
        rtc_peer_mock = rtc_peers.get();
        nvs_peer_mock = nvs_peers.get();
        rtc_channel_mock = rtc_channel.get();
        nvs_channel_mock = nvs_channel.get();
        manager = std::make_unique<StorageManager>(
            std::move(rtc_peers), std::move(nvs_peers), std::move(rtc_channel), std::move(nvs_channel));
    }
};

// Helper to generate a list of dummy peers for testing.
// Using MAX_PEERS * 2 to allow overflow to test max limit
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

// -------------------------------------------------------------------
// Load channel
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, ChannelLoadingFromRtcWhenValid)
{
    // Make a valid data struct
    auto valid_data = create_valid_persistent_channel(6);

    // RTC returns valid data
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    // NVS is not called
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
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
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
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_NE(ESP_OK, manager->load_channel(channel));
}

TEST_F(StorageManagerTest, ChannelLoadingFromNvsWithChannelSyncsRtc)
{
    auto valid_data = create_valid_persistent_channel(6);

    // RTC fails to load data
    ON_CALL(*rtc_channel_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    // NVS returns valid data
    EXPECT_CALL(*nvs_channel_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    // RTC is synced with NVS
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

    // RTC returns the same channel
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
    // RTC fails to load data
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
    // Make a valid data struct
    auto valid_data = create_valid_persistent_peers(6);

    // RTC returns valid data
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    // NVS is not called
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).Times(0);

    EXPECT_EQ(ESP_OK, manager->load_peers(peers));
    EXPECT_EQ(6, peers.size());
}

TEST_F(StorageManagerTest, LoadPeersFromNvsWhenRtcFails)
{
    // Make a valid data struct
    auto valid_data = create_valid_persistent_peers(6);

    // RTC returns invalid data
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));

    // Fallback to NVS
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
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
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
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
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
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).WillOnce([&](void* data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });
    EXPECT_NE(ESP_OK, manager->load_peers(peers));
}

TEST_F(StorageManagerTest, LoadTruncatesWhenDataExceedsVectorCapacity)
{
    // 1. Prepare data with more peers than the vector can hold
    auto oversized_peers_data = create_valid_persistent_peers(MAX_PEERS + 6);

    // Fill the array with dummy peers to avoid reading uninitialized memory during CRC
    for (int i = 0; i < MAX_PEERS; ++i) {
        memset(oversized_peers_data.peers[i].mac, i, 6);
    }

    // Calculate CRC based on this oversized structure
    oversized_peers_data.crc = manager->calculate_crc(oversized_peers_data);

    // 2. Mock NVS to return this oversized data
    // RTC fails to force NVS path
    EXPECT_CALL(*rtc_peer_mock, load(_, _)).Times(1).WillOnce(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_peer_mock, load(_, _)).Times(1).WillOnce([&](void* data, size_t size) {
        memcpy(data, &oversized_peers_data, size);
        return ESP_OK;
    });

    // The vector is explicitly capped at MAX_PEERS
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;

    esp_err_t err = manager->load_peers(loaded_peers);

    // 4. Verification
    EXPECT_EQ(ESP_OK, err);
    // Ensure the vector only took what it could handle (MAX_PEERS)
    // and didn't overflow or crash
    EXPECT_EQ(MAX_PEERS, loaded_peers.size());
    EXPECT_TRUE(loaded_peers.full());
}

TEST_F(StorageManagerTest, LoadPeersFromNvsWithPeersSyncsRtc)
{
    auto peers = create_test_peers(1);                   // create a peer
    auto peers_data = create_valid_persistent_peers(1);  // create a valid persistent peers data
    peers_data.peers[0] = peers[0];                      // Populate the peers data with the peer
    peers_data.crc = manager->calculate_crc(peers_data); // Calculate the CRC of the peers data

    // RTC fails to load data
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    // NVS returns valid data
    ON_CALL(*nvs_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &peers_data, size);
        return ESP_OK;
    });

    // RTC must be synced with NVS
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(1);

    // Load peers
    ASSERT_EQ(ESP_OK, manager->load_peers(peers));
}

// -------------------------------------------------------------------
// Store peers
// -------------------------------------------------------------------

TEST_F(StorageManagerTest, SaveNewPeerSavesToRtcAndNvs)
{
    auto peers = create_test_peers(1);
    ON_CALL(*rtc_peer_mock, save(_, _)).WillByDefault(Return(ESP_OK));
    ON_CALL(*nvs_peer_mock, save(_, _)).WillByDefault(Return(ESP_OK));
    EXPECT_EQ(ESP_OK, manager->store_peers(peers, false));
}

TEST_F(StorageManagerTest, SaveSamePeersDoesNotSave)
{
    auto peers = create_test_peers(1);                   // create a peer
    auto peers_data = create_valid_persistent_peers(1);  // create a valid persistent peers data
    peers_data.peers[0] = peers[0];                      // Populate the peers data with the peer
    peers_data.crc = manager->calculate_crc(peers_data); // Calculate the CRC of the peers data

    // RTC returns the same PersistentPeers data
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &peers_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(0);
    EXPECT_CALL(*nvs_peer_mock, save(_, _)).Times(0);
    EXPECT_EQ(ESP_OK, manager->store_peers(peers, false));
}

TEST_F(StorageManagerTest, SaveSamePeersWithForceNvsCommitSavesToNvs)
{
    auto peers = create_test_peers(1);                   // create a peer
    auto peers_data = create_valid_persistent_peers(1);  // create a valid persistent peers data
    peers_data.peers[0] = peers[0];                      // Populate the peers data with the peer
    peers_data.crc = manager->calculate_crc(peers_data); // Calculate the CRC of the peers data

    // RTC returns the same PersistentPeers data
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault([&](void* data, size_t size) {
        memcpy(data, &peers_data, size);
        return ESP_OK;
    });
    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(0); // RTC is not called because the data is the same
    EXPECT_CALL(*nvs_peer_mock, save(_, _)).Times(1); // NVS is called because force_nvs_commit is true
    EXPECT_EQ(ESP_OK, manager->store_peers(peers, true));
}

TEST_F(StorageManagerTest, SavePeersWithMorePeersThanMaxTruncatesPeersAtMax)
{
    auto peers = create_test_peers(MAX_PEERS + 1);

    auto peers_data = create_valid_persistent_peers(MAX_PEERS + 1);
    for (int i = 0; i < MAX_PEERS + 1; ++i) {
        peers_data.peers[i] = peers[i];
    }
    peers_data.crc = manager->calculate_crc(peers_data);

    // Save via RTC
    ON_CALL(*rtc_peer_mock, save(_, _)).WillByDefault([&](const void* data, size_t size) {
        memcpy(&peers_data, data, size);
        return ESP_OK;
    });

    EXPECT_EQ(ESP_OK, manager->store_peers(peers, true));
    EXPECT_EQ(MAX_PEERS, peers_data.num_peers);
}

TEST_F(StorageManagerTest, SaveSamePeersButRtcFailsToLoadCallsSave)
{
    auto peers = create_test_peers(1);                   // create a peer
    auto peers_data = create_valid_persistent_peers(1);  // create a valid persistent peers data
    peers_data.peers[0] = peers[0];                      // Populate the peers data with the peer
    peers_data.crc = manager->calculate_crc(peers_data); // Calculate the CRC of the peers data

    // RTC fails to load data
    ON_CALL(*rtc_peer_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));

    EXPECT_CALL(*rtc_peer_mock, save(_, _)).Times(1); // RTC is called because it failed to load data
    EXPECT_CALL(*nvs_peer_mock, save(_, _)).Times(1); // NVS is called because RTC failed to check if data is dirty
    EXPECT_EQ(ESP_OK, manager->store_peers(peers, false));
}
