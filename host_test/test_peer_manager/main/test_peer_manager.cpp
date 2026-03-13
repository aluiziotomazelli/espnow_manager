#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "peer_manager.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_storage_manager.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class PeerManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockWiFiHAL> wifi_hal;
    NiceMock<MockStorageManager> storage;
    std::unique_ptr<PeerManager> manager;

    // Helper to create a unique mac for each peer.
    static void make_mac(uint8_t *mac, uint8_t id)
    {
        memset(mac, 0, 6);
        mac[5] = id;
    }

    void SetUp() override
    {
        // Default happy path
        ON_CALL(wifi_hal, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(wifi_hal, hal_esp_now_del_peer(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(wifi_hal, hal_esp_now_mod_peer(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(storage, save(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(storage, load(_, _)).WillByDefault(Return(ESP_ERR_NOT_FOUND));

        manager = std::make_unique<PeerManager>(storage, wifi_hal);
    }

    static constexpr NodeId ID_2 = 2;
    static constexpr NodeId ID_3 = 3;
    static constexpr NodeId ID_4 = 4;
    static constexpr NodeType PEER = 0x02;
    static constexpr NodeType NODE = 0x03;
};

// ============================================================================
// PeerManager::add peer tests
// ============================================================================

TEST_F(PeerManagerTest, AddPeersSuccessful)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    esp_err_t ret;
    // add(NodeId id, const uint8_t *mac, uint8_t channel, NodeType type, uint32_t heartbeat_interval_ms)
    ret = manager->add(ID_2, mac, PEER, 10);
    EXPECT_EQ(ESP_OK, ret);
}

TEST_F(PeerManagerTest, AddPeerWithNullMacFails)
{
    EXPECT_EQ(ESP_ERR_INVALID_ARG, manager->add(ID_2, nullptr, PEER, 10));
}

TEST_F(PeerManagerTest, AddPeerWithSameIdandMacDontOverwrite)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First call
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));    // add the peer
    auto peers = manager->get_all();                         // Get the peers
    EXPECT_EQ(1, peers.size());                              // Must be only one peer

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(0); // Will not call add again
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));    // Still returns ESP_OK
    peers = manager->get_all();                              // get the peers
    EXPECT_EQ(1, peers.size());                              // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerWithSameIdButDifferentMAcCallsDelAndAdd)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));

    make_mac(mac, 99); // Change mac

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_OK)); // Must call add
    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).Times(1); // And call del if add returns ESP_OK
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));    // Nwe MAC but same ID
    EXPECT_EQ(1, manager->get_all().size());                 // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerSameIdDifferentMAcFailsAndReturnsError)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));

    make_mac(mac, 99); // Change mac

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_FAIL)); // Must call add
    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).Times(0);                            // Must not call del
    EXPECT_EQ(ESP_FAIL, manager->add(ID_2, mac, PEER, 10));                             // ESP_FAIL propagates
    EXPECT_EQ(1, manager->get_all().size());                                            // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerWithSameIdButDifferentChannelCallsMod)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    uint8_t ch_1 = 1;
    uint8_t ch_2 = 2;

    // add(...) compares it->channel with current_channel_: compares
    // current_channel_ saved via manager->set_channel with peer channel
    // If it->channel == current_channel_ it will call esp_now_add_peer()
    // If it->channel != current_channel_ it will call esp_now_mod_peer()

    manager->set_channel(ch_1);                              // Set channel
    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));

    manager->set_channel(ch_2);                              // Change channel
    EXPECT_CALL(wifi_hal, hal_esp_now_mod_peer(_)).Times(1); // Must call mod
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));    // New channel
    EXPECT_EQ(1, manager->get_all().size());                 // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeersFailsAndDoesNotIncludePeer)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_FAIL)); // If Fails
    EXPECT_EQ(ESP_FAIL, manager->add(ID_2, mac, PEER, 10));
    EXPECT_EQ(0, manager->get_all().size()); // Must be no peers
}

TEST_F(PeerManagerTest, AddExactlyMaxPeers)
{
    for (int i = 0; i < MAX_PEERS; i++) {
        uint8_t mac[6];
        make_mac(mac, i);
        ON_CALL(wifi_hal, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_OK));
        EXPECT_EQ(ESP_OK, manager->add((NodeId)i, mac, PEER, 10));
    }

    auto peers = manager->get_all();
    EXPECT_EQ(MAX_PEERS, peers.size());

    for (int i = 0; i < MAX_PEERS; i++) {
        NodeId expected_id = (NodeId)(MAX_PEERS - 1 - i);
        EXPECT_EQ(expected_id, peers[i].node_id) << "Mismatch at index " << i;
    }
}

TEST_F(PeerManagerTest, AddBeyondMaxRemovesPeerWithOldestLastSeen)
{
    // Add MAX_PEERS peers
    for (int i = 0; i < MAX_PEERS; i++) {
        uint8_t mac[6];
        make_mac(mac, i);
        manager->add((NodeId)i, mac, PEER, 10);
    }

    // Change last_seen from peers
    for (int i = 0; i < MAX_PEERS; i++) {
        manager->update_last_seen((NodeId)i, i * 10);
    }

    // Add a new peer ID = 99
    uint8_t new_mac[6];
    make_mac(new_mac, 99);
    manager->add((NodeId)99, new_mac, PEER, 10);

    EXPECT_EQ(MAX_PEERS, manager->get_all().size());

    // ID=0 should be removed
    uint8_t found_mac[6];
    EXPECT_FALSE(manager->find_mac(0, found_mac));
    // ID=18 should be kept (the newest, with higher last_seen timestamp)
    EXPECT_TRUE(manager->find_mac(18, found_mac));
    // ID=99 should be present
    EXPECT_TRUE(manager->find_mac(99, found_mac));
}

// =========================================================================
// PeerManager::remove
// =========================================================================

TEST_F(PeerManagerTest, RemovePeerCallsDel)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));
    EXPECT_EQ(1, manager->get_all().size());

    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).Times(1); // Must call del
    EXPECT_EQ(ESP_OK, manager->remove(ID_2));
    EXPECT_EQ(0, manager->get_all().size()); // Must be no peers
}

TEST_F(PeerManagerTest, RemoveNonExistentPeerDoesNotCallDel)
{
    // Add MAX_PEERS peers
    for (int i = 0; i < MAX_PEERS; i++) {
        uint8_t mac[6];
        make_mac(mac, i);
        manager->add((NodeId)i, mac, PEER, 10);
    }
    EXPECT_EQ(MAX_PEERS, manager->get_all().size()); // Must be MAX_PEERS peers

    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).Times(0); // Should not call del
    EXPECT_EQ(ESP_ERR_NOT_FOUND, manager->remove(99));       // ID_99 does not exist
    EXPECT_EQ(MAX_PEERS, manager->get_all().size());         // Must still be MAX_PEERS peers
}

TEST_F(PeerManagerTest, RemoveReturnsErrorWhenDelFailsAndKeepsPeer)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 10);

    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).WillOnce(Return(ESP_FAIL)); // esp_now_del_peer fails
    EXPECT_EQ(ESP_FAIL, manager->remove(ID_2));                                // Must return error

    // Peer should still be present
    EXPECT_EQ(1, manager->get_all().size()); // Must still be one peer
    uint8_t found_mac[6];
    EXPECT_TRUE(manager->find_mac(ID_2, found_mac)); // Must find peer
}

// =========================================================================
// PeerManager::find_mac
// =========================================================================

TEST_F(PeerManagerTest, ExistingPeerReturnsMac)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 10); // Add peer

    uint8_t found_mac[6];                            // Buffer to store found mac
    EXPECT_TRUE(manager->find_mac(ID_2, found_mac)); // Must find peer
    EXPECT_EQ(0, memcmp(mac, found_mac, 6));         // Must find the same mac
}

TEST_F(PeerManagerTest, NonExistentPeerReturnsFalse)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 10); // Add peer ID_2

    uint8_t found_mac[6];
    EXPECT_FALSE(manager->find_mac(ID_3, found_mac)); // Must not find peer ID_3
}

TEST_F(PeerManagerTest, FindMacWithNullptrDoesNotCrash)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 10); // add com mac válido

    EXPECT_NO_FATAL_FAILURE(manager->find_mac(ID_2, nullptr)); // não crasha
}

// =========================================================================
// PeerManager::get_offline
// =========================================================================

TEST_F(PeerManagerTest, GetOfflineReturnsPeerWhenExpired)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    uint32_t heartbeat_ms = 1000;
    manager->add(ID_2, mac, PEER, heartbeat_ms);

    uint64_t last_seen = 100;
    manager->update_last_seen(ID_2, last_seen);

    // timeout = heartbeat_ms * HEARTBEAT_OFFLINE_MULTIPLIER
    // now = last_seen + timeout + 1 -> expired
    uint64_t now = last_seen + (heartbeat_ms * HEARTBEAT_OFFLINE_MULTIPLIER) + 1;
    auto offline = manager->get_offline(now);

    ASSERT_EQ(1, offline.size());
    EXPECT_EQ(ID_2, offline[0]);
}

TEST_F(PeerManagerTest, GetOfflineDoesNotReturnPeerWhenNotExpired)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    uint32_t heartbeat_ms = 1000;
    manager->add(ID_2, mac, PEER, heartbeat_ms);

    uint64_t last_seen = 100;
    manager->update_last_seen(ID_2, last_seen);

    // if (p.last_seen_ms > 0 && (now_ms - p.last_seen_ms > timeout))
    // now = last_seen + timeout -> still within timeout
    uint64_t now = last_seen + (heartbeat_ms * HEARTBEAT_OFFLINE_MULTIPLIER);
    EXPECT_TRUE(manager->get_offline(now).empty());
}

TEST_F(PeerManagerTest, GetOfflineReturnsEmptyWhenNopeersAdded)
{
    EXPECT_TRUE(manager->get_offline(9999).empty());
}

TEST_F(PeerManagerTest, GetOfflineReturnsEmptyWhenLastSeenIsZero)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 1000); // heartbeat_interval = 1000ms

    // last_seen_ms = 0 - never seen from boot, so dont go offline
    EXPECT_TRUE(manager->get_offline(9999).empty());
}

TEST_F(PeerManagerTest, GetOfflineReturnsEmptyWhenHeartbeatIntervalIsZero)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 0); // heartbeat_interval = 0 - never monitor

    manager->update_last_seen(ID_2, 100);            // even if last_seen_ms != 0
    EXPECT_TRUE(manager->get_offline(9999).empty()); // dont go offline
}

// =========================================================================
// PeerManager::load_from_storage
// =========================================================================

TEST_F(PeerManagerTest, LoadFromStorageReturnsErrorWhenEmpty)
{
    // Default ON_CALL already returns ESP_ERR_NOT_FOUND
    uint8_t channel = 0;
    EXPECT_EQ(ESP_ERR_NOT_FOUND, manager->load_from_storage(channel));
    EXPECT_TRUE(manager->get_all().empty());
}

TEST_F(PeerManagerTest, LoadFromStoragePopulatesPeerList)
{
    // Storage returns 2 peers
    std::vector<PersistentPeer> stored;
    PersistentPeer p1 = {};
    p1.node_id = ID_2;
    p1.channel = 6;
    memset(p1.mac, 0xAA, 6);

    PersistentPeer p2 = {};
    p2.node_id = ID_3;
    p2.channel = 6;
    memset(p2.mac, 0xBB, 6);

    stored.push_back(p1);
    stored.push_back(p2);

    ON_CALL(storage, load(_, _)).WillByDefault([&](uint8_t &channel, std::vector<PersistentPeer> &peers) {
        channel = 6;
        peers = stored;
        return ESP_OK;
    });

    uint8_t channel = 0;
    EXPECT_EQ(ESP_OK, manager->load_from_storage(channel));
    EXPECT_EQ(6, channel); // channel was updated
    EXPECT_EQ(2, manager->get_all().size());

    // Verify peer data was correctly mapped
    uint8_t found_mac[6];
    EXPECT_TRUE(manager->find_mac(ID_2, found_mac));
    EXPECT_EQ(0, memcmp(found_mac, p1.mac, 6));

    EXPECT_TRUE(manager->find_mac(ID_3, found_mac));
    EXPECT_EQ(0, memcmp(found_mac, p2.mac, 6));
}

TEST_F(PeerManagerTest, LoadFromStorageClearsPreviousPeers)
{
    // Add a peer manually first
    uint8_t mac[6];
    make_mac(mac, ID_4);
    manager->add(ID_4, mac, PEER, 10);
    EXPECT_EQ(1, manager->get_all().size());

    // Storage returns different peer
    PersistentPeer p1 = {};
    p1.node_id = ID_2;
    memset(p1.mac, 0xAA, 6);

    ON_CALL(storage, load(_, _)).WillByDefault([&](uint8_t &channel, std::vector<PersistentPeer> &peers) {
        channel = 1;
        peers = {p1};
        return ESP_OK;
    });

    uint8_t channel = 0;
    EXPECT_EQ(ESP_OK, manager->load_from_storage(channel));

    // ID_4 must be gone, ID_2 must be present
    EXPECT_EQ(1, manager->get_all().size());
    uint8_t found_mac[6];
    EXPECT_FALSE(manager->find_mac(ID_4, found_mac));
    EXPECT_TRUE(manager->find_mac(ID_2, found_mac));
}

TEST_F(PeerManagerTest, PersistCallsSaveToStorage)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    ON_CALL(storage, save(_, _, _)).WillByDefault(Return(ESP_OK));
    manager->add(ID_2, mac, PEER, 10);

    EXPECT_CALL(storage, save(_, _, _)).Times(1);
    manager->persist();
}