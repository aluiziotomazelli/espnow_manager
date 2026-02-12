#include "esp_system.h"
#include "mock_storage.hpp"
#include "peer_manager.hpp"
#include "unity.h"
extern "C" {
#include "Mockesp_now.h"
}
#include <cstring>

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
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add peer
    uint8_t mac1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    esp_err_t err   = pm.add(TestNodeId::TEST_SENSOR_A, mac1, 1, TestNodeType::SENSOR);
    TEST_ASSERT_EQUAL(ESP_OK, err); // should pass if added

    // Find a peer by mac
    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(TestNodeId::TEST_SENSOR_A, found_mac)); // should pass if found
    TEST_ASSERT_EQUAL_MEMORY(mac1, found_mac, 6);                        // MAC found should be the same as added
}

TEST_CASE("PeerManager handles LRU", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Full PeerList to the limit
    for (int i = 0; i < MAX_PEERS; ++i) {
        uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, (uint8_t)i};
        pm.add((TestNodeId)(100 + i), mac, 1, TestNodeType::SENSOR);
    }

    // Check if list is full
    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());

    // Add one more, oldest (ID 100) should be removed
    uint8_t mac_new[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    pm.add((TestNodeId)200, mac_new, 1, TestNodeType::SENSOR);

    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());    // List is still full
    TEST_ASSERT_FALSE(pm.find_mac((NodeId)100, nullptr)); // Oldest peer should be removed
    TEST_ASSERT_TRUE(pm.find_mac((NodeId)200, nullptr));  // New peer should be added
}

TEST_CASE("PeerManager LRU with duplicate when full", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Full PeerList to the limit
    for (int i = 0; i < MAX_PEERS; ++i) {
        uint8_t mac[6] = {0, 0, 0, 0, 0, (uint8_t)i};
        pm.add((TestNodeId)(i), mac, 1, TestNodeType::SENSOR);
    }

    // Add peer that already exists with the same MAC
    uint8_t existing_mac[6] = {0, 0, 0, 0, 0, 1}; // ID 1
    pm.add((TestNodeId)1, existing_mac, 1, TestNodeType::SENSOR);

    // Não deve remover ninguém, apenas mover para frente
    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());
    TEST_ASSERT_EQUAL((NodeId)1, pm.get_all()[0].node_id); // Must be the first
}

TEST_CASE("PeerManager rejects null MAC", "[peer_manager]")
{
    MockStorage storage;
    RealPeerManager pm(storage);

    esp_err_t err = pm.add(TestNodeId::TEST_SENSOR_A, nullptr, 1, TestNodeType::SENSOR);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_EQUAL(0, pm.get_all().size()); // Null MAC should be rejected
}

TEST_CASE("PeerManager detects offline peers", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 1000); // 1s heartbeat

    pm.update_last_seen(TestNodeId::TEST_SENSOR_A, 10000);

    // Offline threshold is 2.5 * 1000 = 2500ms.
    // So at 12501ms it should be offline.

    auto offline = pm.get_offline(12000);
    TEST_ASSERT_EQUAL(0, offline.size());

    offline = pm.get_offline(12501);
    TEST_ASSERT_EQUAL(1, offline.size());
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_A), offline[0]);
}

TEST_CASE("PeerManager ignores peers without heartbeat in offline detection", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 0); // ← heartbeat = 0

    pm.update_last_seen(TestNodeId::TEST_SENSOR_A, 1000);

    // Even after a long time, it should not appear offline
    auto offline = pm.get_offline(999999999);
    TEST_ASSERT_EQUAL(0, offline.size());
}

TEST_CASE("PeerManager ignores peers never seen in offline detection", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR, 5000);

    // Dont call update_last_seen(), so last_seen_ms == 0

    auto offline = pm.get_offline(999999);
    TEST_ASSERT_EQUAL(0, offline.size()); // Never seen peers should be ignored
}

TEST_CASE("PeerManager persists to storage on add", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add a peer
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    // Check if it was saved
    TEST_ASSERT_TRUE(storage.save_called);
    TEST_ASSERT_EQUAL(1, storage.saved_peers.size());
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_A), storage.saved_peers[0].node_id);
    TEST_ASSERT_EQUAL_MEMORY(mac, storage.saved_peers[0].mac, 6);
}

TEST_CASE("PeerManager loads peers from storage", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;

    // Pre populate storage
    PersistentPeer p1;
    memcpy(p1.mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    p1.node_id               = to_node_id(TestNodeId::TEST_SENSOR_A);
    p1.channel               = 6;
    p1.type                  = to_node_type(TestNodeType::SENSOR);
    p1.paired                = true;
    p1.heartbeat_interval_ms = 5000;

    // Save paramn to posterior check
    storage.saved_channel = 6;
    storage.saved_peers.push_back(p1);

    RealPeerManager pm(storage);

    // Load from storage
    uint8_t channel = 0;
    esp_err_t err   = pm.load_from_storage(channel);

    // Check channel
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(6, channel);
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // Check peer
    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(TestNodeId::TEST_SENSOR_A, found_mac));
    TEST_ASSERT_EQUAL_MEMORY(p1.mac, found_mac, 6);
}

TEST_CASE("PeerManager updates existing peer", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add peer
    uint8_t mac_old[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(TestNodeId::TEST_SENSOR_A, mac_old, 1, TestNodeType::SENSOR);

    // Change mac from same peerId (simulating changin a vroken device with a new one)
    uint8_t mac_new[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac_new, 1, TestNodeType::SENSOR); // Same NodeId!

    // Must be only one peer, not duplicated
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // MAC must be the new one
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

    // Add a peer
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    // Check if it was saved
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // Remove the peer
    esp_err_t err = pm.remove(TestNodeId::TEST_SENSOR_A);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Check if it was removed
    TEST_ASSERT_EQUAL(0, pm.get_all().size());
    TEST_ASSERT_FALSE(pm.find_mac(TestNodeId::TEST_SENSOR_A, nullptr));
}

TEST_CASE("PeerManager returns error removing non-existent peer", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add a peer
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    // Check if it was saved
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // Try to remove non-existent peer
    esp_err_t err = pm.remove(TestNodeId::TEST_SENSOR_B);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
}

TEST_CASE("PeerManager saves on every add", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Save two peers
    uint8_t mac1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t mac2[6] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    pm.add(TestNodeId::TEST_SENSOR_A, mac1, 1, TestNodeType::SENSOR);
    pm.add(TestNodeId::TEST_SENSOR_B, mac2, 1, TestNodeType::SENSOR);

    // Should have been saved twice
    TEST_ASSERT_EQUAL(2, storage.save_call_count);
}

TEST_CASE("PeerManager moves re-added peer to front (LRU)", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add three peers
    uint8_t mac1[6] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    uint8_t mac2[6] = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
    uint8_t mac3[6] = {0x03, 0x03, 0x03, 0x03, 0x03, 0x03};

    pm.add(TestNodeId::TEST_HUB, mac1, 1, TestNodeType::HUB);
    pm.add(TestNodeId::TEST_SENSOR_A, mac2, 1, TestNodeType::SENSOR);
    pm.add(TestNodeId::TEST_SENSOR_B, mac3, 1, TestNodeType::SENSOR);

    auto peers = pm.get_all();
    // The last peer add must be first on vector list
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_SENSOR_B), peers[0].node_id);

    // Re adding HUB should move it to the front
    pm.add(TestNodeId::TEST_HUB, mac1, 1, TestNodeType::HUB);

    peers = pm.get_all();
    // HUB must be the first
    TEST_ASSERT_EQUAL(to_node_id(TestNodeId::TEST_HUB), peers[0].node_id);
}

TEST_CASE("PeerManager updates peer channel", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add a peer with channel 1
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 1, TestNodeType::SENSOR);

    // Change channel to 6 but same MAC
    pm.add(TestNodeId::TEST_SENSOR_A, mac, 6, TestNodeType::SENSOR);

    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(1, peers.size());
    TEST_ASSERT_EQUAL(6, peers[0].channel); // Must be channel 6
}

TEST_CASE("PeerManager handles storage save failure", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

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

    // pm.add with save (to storage) failure, still returns ESP_OK
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, pm.get_all().size());     // Peer list has one peer
    TEST_ASSERT_EQUAL(0, storage.save_call_count); // Storage NVS has not saved
}

TEST_CASE("PeerManager handles MAC update failure gracefully", "[peer_manager]")
{
    // Fisrt call is OK
    esp_now_add_peer_ExpectAndReturn(nullptr, ESP_OK);
    esp_now_add_peer_IgnoreArg_peer();

    // Second call IS FAIL
    esp_now_add_peer_ExpectAndReturn(nullptr, ESP_FAIL);
    esp_now_add_peer_IgnoreArg_peer();

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac_old[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(TestNodeId::TEST_SENSOR_A, mac_old, 1, TestNodeType::SENSOR); // OK

    uint8_t mac_new[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    esp_err_t err      = pm.add(TestNodeId::TEST_SENSOR_A, mac_new, 1, TestNodeType::SENSOR); // FAIL

    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_EQUAL(1, storage.save_call_count); // Must be only one saved peer

    // MAC must be the old one
    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(TestNodeId::TEST_SENSOR_A, found_mac));
    TEST_ASSERT_EQUAL_MEMORY(mac_old, found_mac, 6); // Same MAC
}
