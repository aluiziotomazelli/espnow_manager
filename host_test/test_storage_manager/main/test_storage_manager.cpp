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
    MOCK_METHOD(esp_err_t, load, (void *data, size_t size), (override));
    MOCK_METHOD(esp_err_t, save, (const void *data, size_t size), (override));
};

class StorageManagerTest : public ::testing::Test
{
protected:
    MockPersistenceBackend *rtc_mock; // raw pointer to configure expects
    MockPersistenceBackend *nvs_mock;
    std::unique_ptr<StorageManager> manager;

    uint8_t channel = 0;
    etl::vector<PersistentPeer, MAX_PEERS> peers;

    void SetUp() override
    {
        auto rtc = std::make_unique<NiceMock<MockPersistenceBackend>>();
        auto nvs = std::make_unique<NiceMock<MockPersistenceBackend>>();
        rtc_mock = rtc.get();
        nvs_mock = nvs.get();
        manager = std::make_unique<StorageManager>(std::move(rtc), std::move(nvs));
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
        p.channel = 1;
        p.type = 2; // SENSOR
        memset(p.mac, i, 6);
        peers.push_back(p);
    }
    return peers;
}

// ===================================================================
// Save Tests
// ===================================================================

TEST_F(StorageManagerTest, NvsFailPropagatesError)
{
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault(Return(ESP_FAIL)); // dirty = true calls nvs commit
    EXPECT_CALL(*rtc_mock, save(_, _)).Times(1);
    EXPECT_CALL(*nvs_mock, save(_, _)).Times(1).WillOnce(Return(ESP_FAIL));
    EXPECT_EQ(ESP_FAIL, manager->save(channel, peers));
}

TEST_F(StorageManagerTest, SaveDifferentDataSavesBothRtcAndNvs)
{
    uint8_t ch_1 = 1;
    uint8_t ch_2 = 2;

    // PersistentData with valid magic, version and crc, different channel
    PersistentData valid_data = {};
    valid_data.magic = PersistentData::MAGIC;
    valid_data.version = PersistentData::VERSION;
    valid_data.wifi_channel = ch_1;
    valid_data.num_peers = 0;
    valid_data.crc = StorageManager::calculate_crc(valid_data);

    ON_CALL(*rtc_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    EXPECT_CALL(*rtc_mock, save(_, _)).Times(1);
    EXPECT_CALL(*nvs_mock, save(_, _)).Times(1).WillOnce(Return(ESP_OK));

    EXPECT_EQ(ESP_OK, manager->save(ch_2, peers, true)); // Save to different channel
}

TEST_F(StorageManagerTest, SaveSameDataDontSaveIfNotDirty)
{
    uint8_t ch_1 = 1;

    // PersistentData with valid magic, version and crc, different channel
    PersistentData valid_data = {};
    valid_data.magic = PersistentData::MAGIC;
    valid_data.version = PersistentData::VERSION;
    valid_data.wifi_channel = ch_1;
    valid_data.num_peers = 0;
    valid_data.crc = StorageManager::calculate_crc(valid_data);

    ON_CALL(*rtc_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    // Same data — not dirty, not saved
    EXPECT_CALL(*rtc_mock, save(_, _)).Times(0);          // dont call rtc save
    EXPECT_CALL(*nvs_mock, save(_, _)).Times(0);          // dont call nvs save
    EXPECT_EQ(ESP_OK, manager->save(ch_1, peers, false)); // force_commit = false
}

TEST_F(StorageManagerTest, ForcingCommitSavesEvenWithSameData)
{
    uint8_t ch_1 = 1;

    // PersistentData with valid magic, version and crc, different channel
    PersistentData valid_data = {};
    valid_data.magic = PersistentData::MAGIC;
    valid_data.version = PersistentData::VERSION;
    valid_data.wifi_channel = ch_1;
    valid_data.num_peers = 0;
    valid_data.crc = StorageManager::calculate_crc(valid_data);

    ON_CALL(*rtc_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    // Same data — not dirty, not saved
    EXPECT_CALL(*rtc_mock, save(_, _)).Times(0);         // dont call rtc save
    EXPECT_CALL(*nvs_mock, save(_, _)).Times(1);         // force commit must call nvs save
    EXPECT_EQ(ESP_OK, manager->save(ch_1, peers, true)); // force_commit = true
}

// ===================================================================
// Load Tests
// ===================================================================

TEST_F(StorageManagerTest, LoadFromRtcWhenValid)
{
    // PersistentData with valid magic, version and crc
    PersistentData valid_data = {};
    valid_data.magic = PersistentData::MAGIC;
    valid_data.version = PersistentData::VERSION;
    valid_data.wifi_channel = 6;
    valid_data.num_peers = 0;
    valid_data.crc = StorageManager::calculate_crc(valid_data);

    // RTC returns valid data
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    // NVS is not called
    EXPECT_CALL(*nvs_mock, load(_, _)).Times(0);

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_OK, manager->load(ch, loaded_peers));
    EXPECT_EQ(6, ch);
}

TEST_F(StorageManagerTest, LoadFromRtcFailCallsNvsLoad)
{
    uint8_t ch = 0;

    // PersistentData with valid magic, version and crc
    PersistentData valid_data = {};
    valid_data.magic = PersistentData::MAGIC;
    valid_data.version = PersistentData::VERSION;
    valid_data.wifi_channel = 6;
    valid_data.num_peers = ch;
    valid_data.crc = StorageManager::calculate_crc(valid_data);

    // Load from RTC fails
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));                     // ESP_FAIL
    EXPECT_CALL(*nvs_mock, load(_, _)).Times(1).WillOnce([&](void *data, size_t size) { // fallback to NVS
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_OK, manager->load(ch, loaded_peers));
}

TEST_F(StorageManagerTest, LoadWithInvalidCrcCallsNvsLoad)
{
    // PersistentData with invalid crc
    PersistentData invalid_data = {};
    invalid_data.crc = 0;

    // RTC returns ESP_OK but with invalid data
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &invalid_data, size);
        return ESP_OK;
    });

    // PersistentData valid from NVS
    PersistentData valid_data = {};
    valid_data.magic = PersistentData::MAGIC;
    valid_data.version = PersistentData::VERSION;
    valid_data.wifi_channel = 3;
    valid_data.num_peers = 0;
    valid_data.crc = StorageManager::calculate_crc(valid_data);

    // NVS is called
    EXPECT_CALL(*nvs_mock, load(_, _)).Times(1).WillOnce([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_OK, manager->load(ch, loaded_peers));
    EXPECT_EQ(3, ch);
}

TEST_F(StorageManagerTest, LoadNvsFailsPropagatesError)
{
    // RTC returns ESP_OK
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault(Return(ESP_OK));

    // nvs returns ESP_FAIL
    EXPECT_CALL(*nvs_mock, load(_, _)).Times(1).WillOnce(Return(ESP_FAIL));

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_FAIL, manager->load(ch, loaded_peers)); // ESP_ERR_NOT_FOUND
}

TEST_F(StorageManagerTest, LoadWithWrongMagicReturnsError)
{
    // PersistentData valid with invalid magic
    PersistentData valid_data = {};
    valid_data.magic = 0xDEADBEEF; // Wrong magic

    // RTC is called
    EXPECT_CALL(*rtc_mock, load(_, _)).Times(1).WillOnce([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    // NVS is called
    EXPECT_CALL(*nvs_mock, load(_, _)).Times(1).WillOnce([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_ERR_INVALID_STATE, manager->load(ch, loaded_peers));
}

TEST_F(StorageManagerTest, LoadWithWrongVersionReturnsError)
{
    // PersistentData valid with invalid version
    PersistentData valid_data = {};
    valid_data.magic = PersistentData::MAGIC; // Valid magic
    valid_data.version = 0x12345678;          // Wrong version

    // RTC is called
    EXPECT_CALL(*rtc_mock, load(_, _)).Times(1).WillOnce([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    // NVS is called
    EXPECT_CALL(*nvs_mock, load(_, _)).Times(1).WillOnce([&](void *data, size_t size) {
        memcpy(data, &valid_data, size);
        return ESP_OK;
    });

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_ERR_INVALID_VERSION, manager->load(ch, loaded_peers));
}

TEST_F(StorageManagerTest, LoadWithInvalidRtcReturnsError)
{
    // PersistentData with invalid crc
    PersistentData invalid_data = {};
    invalid_data.magic = PersistentData::MAGIC;     // Valid magic
    invalid_data.version = PersistentData::VERSION; // Valid version
    invalid_data.crc = 0;                           // Invalid crc

    // RTC returns ESP_OK but with invalid crc data
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &invalid_data, size);
        return ESP_OK;
    });

    // This should trigger NVS -> returns ESP_OK but with invalid crcdata
    ON_CALL(*nvs_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &invalid_data, size);
        return ESP_OK;
    });

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;
    EXPECT_EQ(ESP_ERR_INVALID_CRC, manager->load(ch, loaded_peers));
}

TEST_F(StorageManagerTest, LoadTruncatesWhenDataExceedsVectorCapacity)
{
    // 1. Prepare data with more peers than the vector can hold
    // Assuming MAX_PEERS is 19 for this example
    PersistentData oversized_data = {};
    oversized_data.magic = PersistentData::MAGIC;
    oversized_data.version = PersistentData::VERSION;
    oversized_data.wifi_channel = 11;

    // Set num_peers to a value greater than MAX_PEERS (e.g., 25)
    oversized_data.num_peers = MAX_PEERS + 6;

    // Fill the array with dummy peers to avoid reading uninitialized memory during CRC
    for (int i = 0; i < MAX_PEERS; ++i) {
        memset(oversized_data.peers[i].mac, i, 6);
    }

    // Calculate CRC based on this oversized structure
    oversized_data.crc = manager->calculate_crc(oversized_data);

    // 2. Mock NVS to return this oversized data
    // RTC fails to force NVS path
    EXPECT_CALL(*rtc_mock, load(_, _)).Times(1).WillOnce(Return(ESP_FAIL));
    EXPECT_CALL(*nvs_mock, load(_, _)).Times(1).WillOnce([&](void *data, size_t size) {
        memcpy(data, &oversized_data, size);
        return ESP_OK;
    });

    // 3. Execution
    uint8_t ch = 0;
    // The vector is explicitly capped at MAX_PEERS
    etl::vector<PersistentPeer, MAX_PEERS> loaded_peers;

    esp_err_t err = manager->load(ch, loaded_peers);

    // 4. Verification
    EXPECT_EQ(ESP_OK, err);
    EXPECT_EQ(11, ch);
    // Ensure the vector only took what it could handle (MAX_PEERS)
    // and didn't overflow or crash
    EXPECT_EQ(MAX_PEERS, loaded_peers.size());
    EXPECT_TRUE(loaded_peers.full());
}

// ===========================================================================
// Peers tests
// ===========================================================================

TEST_F(StorageManagerTest, SaveAndLoadWithPeers)
{
    // Create peers
    auto peers_to_save = create_test_peers(3);
    PersistentData saved_data = {};

    // No peers yet, load will fail
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    // Save via RTC
    ON_CALL(*rtc_mock, save(_, _)).WillByDefault([&](const void *data, size_t size) {
        memcpy(&saved_data, data, size);
        return ESP_OK;
    });
    ON_CALL(*nvs_mock, save(_, _)).WillByDefault(Return(ESP_OK));

    // Save peers
    ASSERT_EQ(ESP_OK, manager->save(6, peers_to_save));

    // Loading via RTC with valid data
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &saved_data, size);
        return ESP_OK;
    });

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded;
    ASSERT_EQ(ESP_OK, manager->load(ch, loaded));
    EXPECT_EQ(6, ch);
    ASSERT_EQ(3u, loaded.size());
    EXPECT_EQ(10, loaded[0].node_id); // create_test_peers saves node_id = 10 +i
    EXPECT_EQ(12, loaded[2].node_id);
}

TEST_F(StorageManagerTest, SaveTruncatesPeersAtMax)
{
    // MAX_PEERS + 5 peers — may be truncated
    auto peers_to_save = create_test_peers(MAX_PEERS + 5);
    PersistentData saved_data = {};

    // No peers yet, load will fail
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    // Save via RTC
    ON_CALL(*rtc_mock, save(_, _)).WillByDefault([&](const void *data, size_t size) {
        memcpy(&saved_data, data, size);
        return ESP_OK;
    });
    // Save peers via NVS
    ON_CALL(*nvs_mock, save(_, _)).WillByDefault(Return(ESP_OK));

    // Save peers
    ASSERT_EQ(ESP_OK, manager->save(1, peers_to_save));
    // Check that we have only MAX_PEERS
    EXPECT_EQ(MAX_PEERS, saved_data.num_peers);
}

TEST_F(StorageManagerTest, LoadFromNvsWithPeersSyncsRtc)
{
    auto peers_to_save = create_test_peers(2);
    PersistentData nvs_data = {};
    nvs_data.magic = PersistentData::MAGIC;
    nvs_data.version = PersistentData::VERSION;
    nvs_data.wifi_channel = 11;
    nvs_data.num_peers = 2;
    nvs_data.peers[0] = peers_to_save[0];
    nvs_data.peers[1] = peers_to_save[1];
    nvs_data.crc = StorageManager::calculate_crc(nvs_data);

    // First load from RTC will fail
    ON_CALL(*rtc_mock, load(_, _)).WillByDefault(Return(ESP_FAIL));
    // NVS returns valid data
    ON_CALL(*nvs_mock, load(_, _)).WillByDefault([&](void *data, size_t size) {
        memcpy(data, &nvs_data, size);
        return ESP_OK;
    });

    // RTC must be synced with NVS
    EXPECT_CALL(*rtc_mock, save(_, _)).Times(1);

    uint8_t ch = 0;
    etl::vector<PersistentPeer, MAX_PEERS> loaded;

    ASSERT_EQ(ESP_OK, manager->load(ch, loaded));           // call load
    EXPECT_EQ(11, ch);                                      // channel should be synced
    ASSERT_EQ(2u, loaded.size());                           // peers should be synced
    EXPECT_EQ(peers_to_save[0].node_id, loaded[0].node_id); // peers should be synced
}