#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "peer_manager.hpp"
#include "mock_en_hal_espnow.hpp"
#include "mock_en_hal_freertos.hpp"
#include "mock_storage_manager.hpp"
using namespace espnow;

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class PeerManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockEspNowHAL> espnow_hal;
    NiceMock<MockStorageManager> storage;
    NiceMock<MockFreeRTOSHAL> freertos_hal;
    std::unique_ptr<PeerManager> manager;

    // Fake handle
    SemaphoreHandle_t fake_mutex_ = reinterpret_cast<SemaphoreHandle_t>(0xDEAD);

    // Helper to create a unique mac for each peer.
    static void make_mac(uint8_t* mac, uint8_t id)
    {
        memset(mac, 0, 6);
        mac[5] = id;
    }

    void SetUp() override
    {
        // Mutex lifecycle
        ON_CALL(freertos_hal, mutex_create()).WillByDefault(Return(fake_mutex_));
        ON_CALL(freertos_hal, semaphore_delete(_)).WillByDefault(Return());

        // All public methods take/give the mutex — simulate successful acquisition
        ON_CALL(freertos_hal, semaphore_take(_, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(freertos_hal, semaphore_give(_)).WillByDefault(Return(pdTRUE));

        // EspNowHAL happy path
        ON_CALL(espnow_hal, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(espnow_hal, hal_esp_now_del_peer(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(espnow_hal, hal_esp_now_mod_peer(_)).WillByDefault(Return(ESP_OK));

        // Storage happy path
        ON_CALL(storage, store_peers(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(storage, load_peers(_)).WillByDefault(Return(ESP_ERR_NOT_FOUND));

        manager = std::make_unique<PeerManager>(storage, espnow_hal, freertos_hal);
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

    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(1); // First call
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));      // add the peer
    auto peers = manager->get_all();                           // Get the peers
    EXPECT_EQ(1, peers.size());                                // Must be only one peer

    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(0); // Will not call add again
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));      // Still returns ESP_OK
    peers = manager->get_all();                                // get the peers
    EXPECT_EQ(1, peers.size());                                // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerWithSameIdButDifferentMAcCallsDelAndAdd)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));

    make_mac(mac, 99); // Change mac

    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_)).Times(1).WillOnce(Return(ESP_OK)); // Must call del
    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(1); // And call add if del returns ESP_OK
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));      // Nwe MAC but same ID
    EXPECT_EQ(1, manager->get_all().size());                   // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerWithSameMacButDifferentIdReassigns)
{
    // First, add a peer with ID=2 and MAC=0x02
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(1);
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));

    // Verify peer was added with ID=2
    auto peers = manager->get_all();
    EXPECT_EQ(1, peers.size());
    EXPECT_EQ(ID_2, peers[0].node_id);

    // Now add a NEW ID=3 with the SAME MAC=0x02
    // This should trigger reassign_mac_to_new_id - no driver calls needed
    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(0); // Should NOT call add
    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_)).Times(0); // Should NOT call del
    EXPECT_EQ(ESP_OK, manager->add(ID_3, mac, NODE, 20));      // Same MAC, different ID

    // Should still have only 1 peer
    peers = manager->get_all();
    EXPECT_EQ(1, peers.size());

    // The peer should now have ID=3 (reassigned)
    EXPECT_EQ(ID_3, peers[0].node_id) << "Peer should be reassigned to new ID";

    // The type and heartbeat should be updated
    EXPECT_EQ(NODE, peers[0].type) << "Type should be updated to new type";
    EXPECT_EQ(20, peers[0].heartbeat_interval_ms) << "Heartbeat interval should be updated";

    // last_seen_ms should be reset to 0
    EXPECT_EQ(0, peers[0].last_seen_ms) << "last_seen_ms should be reset";

    // ID=2 should no longer be findable
    uint8_t found_mac[6];
    EXPECT_FALSE(manager->find_mac(ID_2, found_mac)) << "Old ID should not be findable";

    // But the MAC should be findable with the new ID
    EXPECT_TRUE(manager->find_mac(ID_3, found_mac));
    EXPECT_EQ(0, memcmp(mac, found_mac, 6));
}

TEST_F(PeerManagerTest, AddSameIdDifferentMAcDelFailsAndReturnsError)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, PEER, 10));

    make_mac(mac, 99); // Change mac

    // To change the MAC we must delete and add again the same peer, but if the  delete fails,
    //  the add will not be called - a conservative behavior if the MAX_PEERS list is full.
    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_))
        .Times(1)
        .WillOnce(Return(ESP_FAIL));                           // First will cal del, but if it fails
    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(0); // then add will not be called
    EXPECT_EQ(ESP_FAIL, manager->add(ID_2, mac, PEER, 10));    // ESP_FAIL propagates
    EXPECT_EQ(1, manager->get_all().size());                   // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeersFailsAndDoesNotIncludePeer)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(espnow_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_FAIL)); // If Fails
    EXPECT_EQ(ESP_FAIL, manager->add(ID_2, mac, PEER, 10));
    EXPECT_EQ(0, manager->get_all().size()); // Must be no peers
}

TEST_F(PeerManagerTest, AddExactlyMaxPeers)
{
    for (int i = 0; i < MAX_PEERS; i++) {
        uint8_t mac[6];
        make_mac(mac, i);
        ON_CALL(espnow_hal, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_OK));
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

TEST_F(PeerManagerTest, DelFailOnAddBeyondMaxDontAddPeer)
{
    // Fill to max
    for (int i = 0; i < MAX_PEERS; i++) {
        uint8_t mac[6];
        make_mac(mac, i);
        manager->add((NodeId)i, mac, PEER, 10);
    }

    // Try to add a new peer, and delete the oldest peer fails
    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_)).WillOnce(Return(ESP_FAIL));

    // New peer
    uint8_t new_mac[6];
    make_mac(new_mac, 99);

    // Add new peer will fail because del fails
    EXPECT_NE(ESP_OK, manager->add((NodeId)99, new_mac, PEER, 10));
    EXPECT_EQ(MAX_PEERS, manager->get_all().size());
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

    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_)).Times(1); // Must call del
    EXPECT_EQ(ESP_OK, manager->remove(ID_2));
    EXPECT_EQ(0, manager->get_all().size()); // Must be no peers
}

TEST_F(PeerManagerTest, RemovePeerStoresRemainingPeersInSnapshot)
{
    // Add two peers so that after removal, snapshot.push_back is exercised
    uint8_t mac2[6], mac3[6];
    make_mac(mac2, ID_2);
    make_mac(mac3, ID_3);

    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac2, PEER, 10));
    EXPECT_EQ(ESP_OK, manager->add(ID_3, mac3, PEER, 10));
    EXPECT_EQ(2, manager->get_all().size());

    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_)).Times(1);
    EXPECT_CALL(storage, store_peers(_, true))
        .WillOnce(Invoke([this](const etl::ivector<PersistentPeer>& peers, bool /*force_nvs_commit*/) {
            EXPECT_EQ(1, peers.size()); // Only remaining peer should be in snapshot
            EXPECT_EQ(ID_2, peers[0].node_id);
            return ESP_OK;
        }));

    EXPECT_EQ(ESP_OK, manager->remove(ID_3));
    EXPECT_EQ(1, manager->get_all().size());
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

    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_)).Times(0); // Should not call del
    EXPECT_EQ(ESP_ERR_NOT_FOUND, manager->remove(99));         // ID_99 does not exist
    EXPECT_EQ(MAX_PEERS, manager->get_all().size());           // Must still be MAX_PEERS peers
}

TEST_F(PeerManagerTest, RemoveReturnsErrorWhenDelFailsAndKeepsPeer)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 10);

    EXPECT_CALL(espnow_hal, hal_esp_now_del_peer(_)).WillOnce(Return(ESP_FAIL)); // esp_now_del_peer fails
    EXPECT_EQ(ESP_FAIL, manager->remove(ID_2));                                  // Must return error

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

TEST_F(PeerManagerTest, GetOfflineReturnsEmptyWhenNoPeersAdded)
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
// PeerManager::update_last_seen — missing branch coverage
// =========================================================================

TEST_F(PeerManagerTest, UpdateLastSeenNonExistentPeerDoesNothing)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 1000);

    // Update last_seen for a peer that does NOT exist
    // The loop in update_last_seen completes without finding the peer
    manager->update_last_seen(99, 5000);

    // Peer ID_2 should still have last_seen_ms = 0 (never updated)
    auto peers = manager->get_all();
    ASSERT_EQ(1, peers.size());
    EXPECT_EQ(0, peers[0].last_seen_ms);
}

// =========================================================================
// PeerManager::load_peers_from_storage
// =========================================================================

TEST_F(PeerManagerTest, LoadFromStorageReturnsErrorWhenEmpty)
{
    // Default ON_CALL already returns ESP_ERR_NOT_FOUND

    EXPECT_EQ(ESP_ERR_NOT_FOUND, manager->load_peers_from_storage());
    EXPECT_TRUE(manager->get_all().empty());
}

TEST_F(PeerManagerTest, LoadFromStoragePopulatesPeerList)
{
    // Storage returns 2 peers
    etl::vector<PersistentPeer, 2> stored;
    PersistentPeer p1 = {};
    p1.node_id = ID_2;
    memset(p1.mac, 0xAA, 6);

    PersistentPeer p2 = {};
    p2.node_id = ID_3;
    memset(p2.mac, 0xBB, 6);

    stored.push_back(p1);
    stored.push_back(p2);

    ON_CALL(storage, load_peers(_)).WillByDefault([&](etl::ivector<PersistentPeer>& peers) {
        peers = stored;
        return ESP_OK;
    });

    EXPECT_EQ(ESP_OK, manager->load_peers_from_storage());
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
    etl::vector<PersistentPeer, 1> stored;
    PersistentPeer p1 = {};
    p1.node_id = ID_2;
    memset(p1.mac, 0xAA, 6);

    stored.push_back(p1);

    EXPECT_CALL(storage, load_peers(_)).WillOnce([&](etl::ivector<PersistentPeer>& peers) {
        peers = stored;
        return ESP_OK;
    });

    EXPECT_EQ(ESP_OK, manager->load_peers_from_storage());

    // ID_4 must be gone, ID_2 must be present
    EXPECT_EQ(1, manager->get_all().size());
    uint8_t found_mac[6];
    EXPECT_FALSE(manager->find_mac(ID_4, found_mac));
    EXPECT_TRUE(manager->find_mac(ID_2, found_mac));
}

// =========================================================================
// PeerManager::save_to_storage (via add, remove)
// =========================================================================

TEST_F(PeerManagerTest, SaveToStorageLogsOnError)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(storage, store_peers(_, _)).WillOnce(Return(ESP_FAIL)); // save_to_storage inside add() returns error

    // add() itself must still return ESP_OK — storage failure is non-fatal
    EXPECT_EQ(ESP_FAIL, manager->add(ID_2, mac, PEER, 10));

    // Peer must still be in the list (storage error doesn't roll back)
    EXPECT_EQ(1, manager->get_all().size());
}

// =========================================================================
// Mutex Guard
// =========================================================================

TEST_F(PeerManagerTest, GetAllDontTakeMutexReturnsEmptyVector)
{
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE)); // semaphore_take fails
    EXPECT_CALL(freertos_hal, semaphore_give(_)).Times(0);                     // semaphore_give should not be called

    EXPECT_EQ(0, manager->get_all().size());
}

TEST_F(PeerManagerTest, UpdateLastSeenDontTakeMutex)
{
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE)); // semaphore_take fails
    EXPECT_CALL(freertos_hal, semaphore_give(_)).Times(0);                     // semaphore_give should not be called

    manager->update_last_seen(ID_2, 10);
}

TEST_F(PeerManagerTest, LoadFromStorageDontTakeMutexReturnsError)
{
    EXPECT_CALL(storage, load_peers(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE)); // semaphore_take fails
    EXPECT_CALL(freertos_hal, semaphore_give(_)).Times(0);                     // semaphore_give should not be called

    EXPECT_EQ(ESP_ERR_TIMEOUT, manager->load_peers_from_storage());
}

TEST_F(PeerManagerTest, AddPeersDontTakeMutexReturnsError)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE));

    EXPECT_EQ(ESP_ERR_TIMEOUT, manager->add(ID_2, mac, PEER, 10));
}

TEST_F(PeerManagerTest, RemoveDontTakeMutexReturnsError)
{
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE));
    EXPECT_EQ(ESP_ERR_TIMEOUT, manager->remove(ID_2));
}

TEST_F(PeerManagerTest, FindMacDontTakeMutexReturnsFalse)
{
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE));
    uint8_t found_mac[6];
    EXPECT_FALSE(manager->find_mac(ID_2, found_mac));
}

TEST_F(PeerManagerTest, GetOfflineDontTakeMutexReturnsEmptyVector)
{
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE)); // semaphore_take fails
    EXPECT_CALL(freertos_hal, semaphore_give(_)).Times(0);                     // semaphore_give should not be called

    auto offline = manager->get_offline(0);
    EXPECT_EQ(0, offline.size());
}

// ===========================================================================
// PeerManager::find_node_id_by_mac
// ===========================================================================

TEST_F(PeerManagerTest, FindNodeIdByMacReturnsIdWhenFound)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 1000);

    NodeId out_id = 0;
    EXPECT_EQ(ESP_OK, manager->find_node_id_by_mac(mac, out_id));
    EXPECT_EQ(ID_2, out_id);
}

TEST_F(PeerManagerTest, FindNodeIdByMacReturnsNotFoundWhenNotFound)
{
    uint8_t unknown_mac[6] = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA};
    NodeId out_id = 0;
    EXPECT_EQ(ESP_ERR_NOT_FOUND, manager->find_node_id_by_mac(unknown_mac, out_id));
    EXPECT_EQ(0, out_id); // out_id should remain unchanged
}

TEST_F(PeerManagerTest, FindNodeIdByMacReturnsTimeoutOnMutexFailure)
{
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE));
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    NodeId out_id = 0;
    EXPECT_EQ(ESP_ERR_TIMEOUT, manager->find_node_id_by_mac(mac, out_id));
}

// ===========================================================================
// PeerManager::is_online
// ===========================================================================

TEST_F(PeerManagerTest, IsOnlineReturnsTrueWhenPeerSeenWithinTimeout)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 1000); // timeout = 1000 * 3 = 3000 ms

    manager->update_last_seen(ID_2, 5000);

    // Seen at 5000, checking at 7000 (elapsed = 2000 ms <= 3000 ms) -> online
    EXPECT_TRUE(manager->is_online(ID_2, 7000));
}

TEST_F(PeerManagerTest, IsOnlineReturnsFalseWhenTimeoutExpired)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 1000); // timeout = 3000 ms

    manager->update_last_seen(ID_2, 5000);

    // Seen at 5000, checking at 8001 (elapsed = 3001 ms > 3000 ms) -> offline
    EXPECT_FALSE(manager->is_online(ID_2, 8001));
}

TEST_F(PeerManagerTest, IsOnlineReturnsFalseWhenPeerNeverSeen)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 1000);

    // last_seen_ms is 0 by default
    EXPECT_FALSE(manager->is_online(ID_2, 5000));
}

TEST_F(PeerManagerTest, IsOnlineReturnsFalseWhenPeerNotFound)
{
    EXPECT_FALSE(manager->is_online(ID_4, 5000));
}

TEST_F(PeerManagerTest, IsOnlineReturnsFalseWhenHeartbeatIntervalIsZero)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, PEER, 0); // interval = 0 (not monitored by timeout)

    manager->update_last_seen(ID_2, 5000);

    EXPECT_FALSE(manager->is_online(ID_2, 5100));
}

TEST_F(PeerManagerTest, IsOnlineReturnsFalseOnMutexFailure)
{
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillOnce(Return(pdFALSE));
    EXPECT_FALSE(manager->is_online(ID_2, 5000));
}