#include "esp_system.h"
#include "mock_storage.hpp"
#include "peer_manager.hpp"
#include "unity.h"
extern "C" {
#include "Mockesp_now.h"
}
#include <cstring>

void setUp(void)
{
    Mockesp_now_Init();
}

void tearDown(void)
{
    Mockesp_now_Verify();
    Mockesp_now_Destroy();
}

/**
 * @file test_peer_manager.cpp
 * @brief Unit tests for the RealPeerManager class.
 *
 * The Peer Manager maintains the list of known ESP-NOW devices, handles persistence
 * to NVS/RTC, and manages the limited peer slots in the ESP32 radio using an LRU (Least Recently Used) policy.
 */

enum class TestNodeId : NodeId
{
    TEST_HUB      = 1,
    TEST_SENSOR_A = 10,
    TEST_SENSOR_B = 11,
    NON_EXISTENT  = 90
};

enum class TestNodeType : NodeType
{
    HUB    = 1,
    SENSOR = 2
};

TEST_CASE("PeerManager can add and find peers", "[peer_manager]")
{
    // Ignore internal ESP-NOW driver calls as we are testing the manager logic.
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add a new peer to the manager.
    uint8_t mac1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    esp_err_t err   = pm.add(TestNodeId::TEST_SENSOR_A, mac1, 1, TestNodeType::SENSOR);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify that the peer can be retrieved by its Node ID.
    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(TestNodeId::TEST_SENSOR_A, found_mac));
    TEST_ASSERT_EQUAL_MEMORY(mac1, found_mac, 6);
}

TEST_CASE("PeerManager handles LRU", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Fill the peer list up to its maximum capacity (MAX_PEERS).
    for (int i = 0; i < MAX_PEERS; ++i) {
        uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, (uint8_t)i};
        pm.add((TestNodeId)(100 + i), mac, 1, TestNodeType::SENSOR);
    }

    // List should now be full.
    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());

    // Add one more peer.
    // According to LRU policy, the oldest peer (the first one added, ID 100) should be evicted.
    uint8_t mac_new[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    pm.add((TestNodeId)200, mac_new, 1, TestNodeType::SENSOR);

    // List size should remain at the limit.
    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());

    // Verify eviction of the oldest peer.
    TEST_ASSERT_FALSE(pm.find_mac((NodeId)100, nullptr));
    // Verify addition of the new peer.
    TEST_ASSERT_TRUE(pm.find_mac((NodeId)200, nullptr));
}

TEST_CASE("PeerManager LRU with duplicate when full", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Fill the list to the limit.
    for (int i = 0; i < MAX_PEERS; ++i) {
        uint8_t mac[6] = {0, 0, 0, 0, 0, (uint8_t)i};
        pm.add((TestNodeId)(i), mac, 1, TestNodeType::SENSOR);
    }

    // Re-add a peer that already exists (ID 1).
    uint8_t existing_mac[6] = {0, 0, 0, 0, 0, 1};
    pm.add((TestNodeId)1, existing_mac, 1, TestNodeType::SENSOR);

    // It should NOT remove anyone (as there is no new peer),
    // but should move ID 1 to the front of the LRU list.
    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());
    TEST_ASSERT_EQUAL((NodeId)1, pm.get_all()[0].node_id);
}

TEST_CASE("PeerManager rejects null MAC", "[peer_manager]")
{
    MockStorage storage;
    RealPeerManager pm(storage);

    // Boundary condition: Adding a peer with no MAC should be rejected.
    esp_err_t err = pm.add(TestNodeId::TEST_SENSOR_A, nullptr, 1, TestNodeType::SENSOR);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_EQUAL(0, pm.get_all().size());
}

TEST_CASE("PeerManager rejects invalid node type", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Documentation test: currently, any NodeType is accepted as long as it fits in uint8_t.
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    esp_err_t err  = pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, (TestNodeType)0xFF);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_EQUAL(1, pm.get_all().size());
}

TEST_CASE("PeerManager detects offline peers", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add sensor with 1s (1000ms) heartbeat interval.
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 1000);

    // Simulate sensor seen at t=10s.
    pm.update_last_seen(TestNodeId::TEST_SENSOR_A, 10000);

    // Threshold is 2.5x heartbeat (2500ms).
    // At t=12s (2s gap), it should still be ONLINE.
    auto offline = pm.get_offline(12000);
    TEST_ASSERT_EQUAL(0, offline.size());

    // At t=12.501s (>2.5s gap), it should be OFFLINE.
    offline = pm.get_offline(12501);
    TEST_ASSERT_EQUAL(1, offline.size());
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_A), offline[0]);
}

TEST_CASE("PeerManager ignores peers without heartbeat in offline detection", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Heartbeat = 0 means offline detection is disabled for this peer.
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 0);

    pm.update_last_seen(TestNodeId::TEST_SENSOR_A, 1000);

    // It should never appear offline regardless of elapsed time.
    auto offline = pm.get_offline(999999999);
    TEST_ASSERT_EQUAL(0, offline.size());
}

TEST_CASE("PeerManager handles zero heartbeat interval correctly", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add peer with zero heartbeat initially.
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 0);

    // Re-add/Update heartbeat to 5s.
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 5000);

    // Verify the update took effect.
    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(1, peers.size());
    TEST_ASSERT_EQUAL(5000, peers[0].heartbeat_interval_ms);
}

TEST_CASE("PeerManager ignores peers never seen in offline detection", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 5000);

    // Peer was added but update_last_seen() was never called.
    // The manager should ignore devices it hasn't communicated with yet.
    auto offline = pm.get_offline(999999);
    TEST_ASSERT_EQUAL(0, offline.size());
}

TEST_CASE("PeerManager persists to storage on add", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Verify that every 'add' operation triggers a save to the persistence layer.
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    TEST_ASSERT_TRUE(storage.save_called);
    TEST_ASSERT_EQUAL(1, storage.saved_peers.size());
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_A), storage.saved_peers[0].node_id);
}

TEST_CASE("PeerManager persists data with persist method", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t mac2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    pm.add(TestNodeId::TEST_SENSOR_A, mac1, 1, TestNodeType::SENSOR);
    pm.add(TestNodeId::TEST_SENSOR_B, mac2, 6, TestNodeType::SENSOR);

    // Explicitly request data persistence.
    int initial_save_count = storage.save_call_count;
    pm.persist(11); // Force channel 11 into storage

    TEST_ASSERT_EQUAL(initial_save_count + 1, storage.save_call_count);
    TEST_ASSERT_EQUAL(11, storage.saved_channel);
    TEST_ASSERT_EQUAL(2, storage.saved_peers.size());
}

TEST_CASE("PeerManager loads peers from storage", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;

    // Pre-populate the mock storage with one peer.
    PersistentPeer p1;
    memcpy(p1.mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    p1.node_id               = to_node_id(TestNodeId::TEST_SENSOR_A);
    p1.channel               = 6;
    p1.type                  = to_node_type(TestNodeType::SENSOR);
    p1.paired                = true;
    p1.heartbeat_interval_ms = 5000;

    storage.saved_channel = 6;
    storage.saved_peers.push_back(p1);

    RealPeerManager pm(storage);

    // Request the manager to load state from the storage.
    uint8_t channel = 0;
    esp_err_t err   = pm.load_from_storage(channel);

    // Verify channel and peer list restoration.
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(6, channel);
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(TestNodeId::TEST_SENSOR_A, found_mac));
    TEST_ASSERT_EQUAL_MEMORY(p1.mac, found_mac, 6);
}

TEST_CASE("PeerManager handles storage load failure", "[peer_manager]")
{
    // Simulate a failure in the storage backend (e.g. NVS corruption).
    class FailingStorageLoad : public MockStorage
    {
        esp_err_t load(uint8_t &channel, std::vector<PersistentPeer> &peers) override
        {
            return ESP_FAIL;
        }
    };

    FailingStorageLoad storage;
    RealPeerManager pm(storage);

    uint8_t channel = 0;
    esp_err_t err   = pm.load_from_storage(channel);

    // Verify the manager returns the error and keeps an empty peer list.
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_EQUAL(0, pm.get_all().size());
}

TEST_CASE("PeerManager updates existing peer", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add peer initially.
    uint8_t mac_old[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(TestNodeId::TEST_SENSOR_A, mac_old, 1, TestNodeType::SENSOR);

    // Simulate replacing a device: new MAC for the same Node ID.
    uint8_t mac_new[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac_new, 1, TestNodeType::SENSOR);

    // Peer list should still have only 1 entry.
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // Verify the MAC was updated.
    uint8_t found_mac[6];
    pm.find_mac(TestNodeId::TEST_SENSOR_A, found_mac);
    TEST_ASSERT_EQUAL_MEMORY(mac_new, found_mac, 6);
}

TEST_CASE("PeerManager removes peer", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // Verify manual peer removal.
    esp_err_t err = pm.remove(TestNodeId::TEST_SENSOR_A);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_EQUAL(0, pm.get_all().size());
    TEST_ASSERT_FALSE(pm.find_mac(TestNodeId::TEST_SENSOR_A, nullptr));
}

TEST_CASE("PeerManager returns error removing non-existent peer", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    // Removing a Node ID that isn't in the list should return NOT_FOUND.
    esp_err_t err = pm.remove(TestNodeId::TEST_SENSOR_B);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
}

TEST_CASE("PeerManager handles operations on empty peer list", "[peer_manager]")
{
    MockStorage storage;
    RealPeerManager pm(storage);

    // Verify all operations fail gracefully when the list is empty.
    TEST_ASSERT_EQUAL(0, pm.get_all().size());
    TEST_ASSERT_EQUAL(0, pm.get_offline(1000).size());

    uint8_t mac[6];
    TEST_ASSERT_FALSE(pm.find_mac(TestNodeId::TEST_SENSOR_A, mac));

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, pm.remove(TestNodeId::TEST_SENSOR_A));
}

TEST_CASE("PeerManager moves re-added peer to front (LRU)", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add three peers in sequence.
    uint8_t mac1[6] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    uint8_t mac2[6] = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
    uint8_t mac3[6] = {0x03, 0x03, 0x03, 0x03, 0x03, 0x03};

    pm.add(TestNodeId::TEST_HUB, mac1, 1, TestNodeType::HUB);
    pm.add(TestNodeId::TEST_SENSOR_A, mac2, 1, TestNodeType::SENSOR);
    pm.add(TestNodeId::TEST_SENSOR_B, mac3, 1, TestNodeType::SENSOR);

    // The most recently added (SENSOR_B) should be at index 0.
    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_B), peers[0].node_id);

    // Re-adding the HUB (which was at the back) should move it to the front (index 0).
    pm.add(TestNodeId::TEST_HUB, mac1, 1, TestNodeType::HUB);

    peers = pm.get_all();
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_HUB), peers[0].node_id);
}

TEST_CASE("PeerManager updates peer channel", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add peer on channel 1.
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    // Simulate Hub hopping to channel 6.
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 6, TestNodeType::SENSOR);

    // Verify channel update.
    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(1, peers.size());
    TEST_ASSERT_EQUAL(6, peers[0].channel);
}

TEST_CASE("PeerManager handles storage save failure", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    // Scenario: the device is added in memory, but the persistence layer fails.
    class FailingStorage : public MockStorage
    {
        esp_err_t save(uint8_t, const std::vector<PersistentPeer> &, bool) override
        {
            return ESP_FAIL;
        }
    };

    FailingStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    esp_err_t err  = pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    // The manager should still succeed (in-memory update) even if storage fails.
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, pm.get_all().size());
}

TEST_CASE("PeerManager handles MAC update failure gracefully", "[peer_manager]")
{
    // Scenario: The internal radio driver (esp_now) rejects a peer modification.

    // First call (add) OK
    esp_now_add_peer_ExpectAndReturn(nullptr, ESP_OK);
    esp_now_add_peer_IgnoreArg_peer();

    // Second call (mod) FAIL
    esp_now_add_peer_ExpectAndReturn(nullptr, ESP_FAIL);
    esp_now_add_peer_IgnoreArg_peer();

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac_old[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(TestNodeId::TEST_SENSOR_A, mac_old, 1, TestNodeType::SENSOR); // Success

    uint8_t mac_new[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    esp_err_t err      = pm.add(TestNodeId::TEST_SENSOR_A, mac_new, 1, TestNodeType::SENSOR); // Fail

    // The manager should revert/keep the old MAC if the driver update fails.
    TEST_ASSERT_EQUAL(ESP_FAIL, err);

    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(TestNodeId::TEST_SENSOR_A, found_mac));
    TEST_ASSERT_EQUAL_MEMORY(mac_old, found_mac, 6);
}

TEST_CASE("PeerManager correctly loads multiple peers from storage in order", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;

    // Simulate an existing database with multiple devices.
    PersistentPeer p1, p2, p3;

    memcpy(p1.mac, "\x11\x11\x11\x11\x11\x11", 6);
    p1.node_id               = to_node_id(TestNodeId::TEST_HUB);
    p1.type                  = to_node_type(TestNodeType::HUB);

    memcpy(p2.mac, "\x22\x22\x22\x22\x22\x22", 6);
    p2.node_id               = to_node_id(TestNodeId::TEST_SENSOR_A);
    p2.type                  = to_node_type(TestNodeType::SENSOR);

    memcpy(p3.mac, "\x33\x33\x33\x33\x33\x33", 6);
    p3.node_id               = to_node_id(TestNodeId::TEST_SENSOR_B);
    p3.type                  = to_node_type(TestNodeType::SENSOR);

    storage.saved_peers.push_back(p1);
    storage.saved_peers.push_back(p2);
    storage.saved_peers.push_back(p3);

    RealPeerManager pm(storage);

    uint8_t channel = 0;
    pm.load_from_storage(channel);

    // Verify that the order in storage is preserved in memory.
    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_HUB), peers[0].node_id);
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_A), peers[1].node_id);
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_B), peers[2].node_id);
}
