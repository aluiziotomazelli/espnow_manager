#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "peer_manager.hpp"
#include "mock_wifi_hal.hpp"
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
    ret = manager->add(ID_2, mac, 0, PEER, 10);
    EXPECT_EQ(ESP_OK, ret);
}

TEST_F(PeerManagerTest, AddPeerWithNullMacFails)
{
    EXPECT_EQ(ESP_ERR_INVALID_ARG, manager->add(ID_2, nullptr, 0, PEER, 10));
}

TEST_F(PeerManagerTest, AddPeerWithSameIdandMacDontOverwrite)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First call
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, 0, PEER, 10)); // add the peer
    auto peers = manager->get_all();                         // Get the peers
    EXPECT_EQ(1, peers.size());                              // Must be only one peer

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(0); // Will not call add again
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, 0, PEER, 10)); // Still returns ESP_OK
    peers = manager->get_all();                              // get the peers
    EXPECT_EQ(1, peers.size());                              // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerWithSameIdButDifferentMAcCallsDelAndAdd)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, 0, PEER, 10));

    make_mac(mac, 99); // Change mac

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_OK)); // Must call add
    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).Times(1); // And call del if add returns ESP_OK
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, 0, PEER, 10)); // Nwe MAC but same ID
    EXPECT_EQ(1, manager->get_all().size());                 // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerSameIdDifferentMAcFailsAndReturnsError)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, 0, PEER, 10));

    make_mac(mac, 99); // Change mac

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_FAIL)); // Must call add
    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).Times(0);                            // Must not call del
    EXPECT_EQ(ESP_FAIL, manager->add(ID_2, mac, 0, PEER, 10));                          // ESP_FAIL propagates
    EXPECT_EQ(1, manager->get_all().size());                                            // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeerWithSameIdButDifferentChannelCallsMod)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    uint8_t ch_1 = 1;
    uint8_t ch_2 = 2;

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1); // First add
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, ch_1, PEER, 10));

    EXPECT_CALL(wifi_hal, hal_esp_now_mod_peer(_)).Times(1);    // Must call mod
    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, ch_2, PEER, 10)); // New channel
    EXPECT_EQ(1, manager->get_all().size());                    // Must be only one peer
}

TEST_F(PeerManagerTest, AddPeersFailsAndDoesNotIncludePeer)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);

    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_FAIL)); // If Fails
    EXPECT_EQ(ESP_FAIL, manager->add(ID_2, mac, 1, PEER, 10));
    EXPECT_EQ(0, manager->get_all().size()); // Must be no peers
}

TEST_F(PeerManagerTest, AddExactlyMaxPeers)
{
    for (int i = 0; i < MAX_PEERS; i++) {
        uint8_t mac[6];
        make_mac(mac, i);
        EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1).WillOnce(Return(ESP_OK));
        EXPECT_EQ(ESP_OK, manager->add((NodeId)i, mac, 1, PEER, 10));
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
        manager->add((NodeId)i, mac, 1, PEER, 10);
    }

    // Change last_seen from peers
    for (int i = 0; i < MAX_PEERS; i++) {
        manager->update_last_seen((NodeId)i, i * 10);
    }

    // Add a new peer ID = 99
    uint8_t new_mac[6];
    make_mac(new_mac, 99);
    manager->add((NodeId)99, new_mac, 1, PEER, 10);

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

    EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, 0, PEER, 10));
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
        manager->add((NodeId)i, mac, 1, PEER, 10);
    }
    EXPECT_EQ(MAX_PEERS, manager->get_all().size()); // Must be MAX_PEERS peers

    // EXPECT_EQ(ESP_OK, manager->add(ID_2, mac, 0, PEER, 10));
    // EXPECT_EQ(1, manager->get_all().size());

    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).Times(0); // Should not call del
    EXPECT_EQ(ESP_ERR_NOT_FOUND, manager->remove(99));       // ID_99 does not exist
    EXPECT_EQ(MAX_PEERS, manager->get_all().size());         // Must still be MAX_PEERS peers
}

TEST_F(PeerManagerTest, RemoveReturnsErrorWhenDelFailsAndKeepsPeer)
{
    uint8_t mac[6];
    make_mac(mac, ID_2);
    manager->add(ID_2, mac, 0, PEER, 10);

    EXPECT_CALL(wifi_hal, hal_esp_now_del_peer(_)).WillOnce(Return(ESP_FAIL)); // esp_now_del_peer fails
    EXPECT_EQ(ESP_FAIL, manager->remove(ID_2));                                // Must return error

    // Peer should still be present
    EXPECT_EQ(1, manager->get_all().size()); // Must still be one peer
    uint8_t found_mac[6];
    EXPECT_TRUE(manager->find_mac(ID_2, found_mac)); // Must find peer
}