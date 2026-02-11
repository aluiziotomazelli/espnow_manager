#include "esp_system.h"
#include "mock_storage.hpp"
#include "peer_manager.hpp"
#include "unity.h"
extern "C" {
#include "Mockesp_now.h"
}
#include <cstring>

TEST_CASE("PeerManager can add and find peers", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    esp_err_t err   = pm.add(NodeId::WATER_TANK, mac1, 1, NodeType::SENSOR);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(NodeId::WATER_TANK, found_mac));
    TEST_ASSERT_EQUAL_MEMORY(mac1, found_mac, 6);
}

TEST_CASE("PeerManager handles LRU", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    for (int i = 0; i < MAX_PEERS; ++i) {
        uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, (uint8_t)i};
        pm.add((NodeId)(100 + i), mac, 1, NodeType::SENSOR);
    }

    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());

    // Add one more, oldest (ID 100) should be removed
    uint8_t mac_new[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    pm.add((NodeId)200, mac_new, 1, NodeType::SENSOR);

    TEST_ASSERT_EQUAL(MAX_PEERS, pm.get_all().size());
    TEST_ASSERT_FALSE(pm.find_mac((NodeId)100, nullptr));
    TEST_ASSERT_TRUE(pm.find_mac((NodeId)200, nullptr));
}

TEST_CASE("PeerManager detects offline peers", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(NodeId::WATER_TANK, mac, 1, NodeType::SENSOR, 1000); // 1s heartbeat

    pm.update_last_seen(NodeId::WATER_TANK, 10000);

    // Offline threshold is 2.5 * 1000 = 2500ms.
    // So at 12501ms it should be offline.

    auto offline = pm.get_offline(12000);
    TEST_ASSERT_EQUAL(0, offline.size());

    offline = pm.get_offline(12501);
    TEST_ASSERT_EQUAL(1, offline.size());
    TEST_ASSERT_EQUAL(NodeId::WATER_TANK, offline[0]);
}

TEST_CASE("PeerManager persists to storage on add", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add a peer
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(NodeId::WATER_TANK, mac, 1, NodeType::SENSOR);

    // Check if it was saved
    TEST_ASSERT_TRUE(storage.save_called);
    TEST_ASSERT_EQUAL(1, storage.saved_peers.size());
    TEST_ASSERT_EQUAL(NodeId::WATER_TANK, storage.saved_peers[0].node_id);
    TEST_ASSERT_EQUAL_MEMORY(mac, storage.saved_peers[0].mac, 6);
}

TEST_CASE("PeerManager loads peers from storage", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;

    // Pre populate storage
    PersistentPeer p1;
    memcpy(p1.mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    p1.node_id               = NodeId::WATER_TANK;
    p1.channel               = 6;
    p1.type                  = NodeType::SENSOR;
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
    TEST_ASSERT_TRUE(pm.find_mac(NodeId::WATER_TANK, found_mac));
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
    pm.add(NodeId::WATER_TANK, mac_old, 1, NodeType::SENSOR);

    // Change mac from same peerId (simulating changin a vroken device with a new one)
    uint8_t mac_new[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(NodeId::WATER_TANK, mac_new, 1, NodeType::SENSOR); // Same NodeId!

    // Must be only one peer, not duplicated
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // MAC must be the new one
    uint8_t found_mac[6];
    pm.find_mac(NodeId::WATER_TANK, found_mac);
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
    pm.add(NodeId::WATER_TANK, mac, 1, NodeType::SENSOR);

    // Check if it was saved
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // Remove the peer
    esp_err_t err = pm.remove(NodeId::WATER_TANK);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Check if it was removed
    TEST_ASSERT_EQUAL(0, pm.get_all().size());
    TEST_ASSERT_FALSE(pm.find_mac(NodeId::WATER_TANK, nullptr));
}

TEST_CASE("PeerManager returns error removing non-existent peer", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add a peer
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(NodeId::WATER_TANK, mac, 1, NodeType::SENSOR);

    // Check if it was saved
    TEST_ASSERT_EQUAL(1, pm.get_all().size());

    // Try to remove non-existent peer
    esp_err_t err = pm.remove(NodeId::SOLAR_SENSOR);
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
    pm.add(NodeId::WATER_TANK, mac1, 1, NodeType::SENSOR);
    pm.add(NodeId::SOLAR_SENSOR, mac2, 1, NodeType::SENSOR);

    // Should have been saved twice
    TEST_ASSERT_EQUAL(2, storage.save_call_count);
}

TEST_CASE("PeerManager moves accessed peer to front (LRU)", "[peer_manager]")
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

    pm.add(NodeId::HUB, mac1, 1, NodeType::HUB);
    pm.add(NodeId::WATER_TANK, mac2, 1, NodeType::SENSOR);
    pm.add(NodeId::SOLAR_SENSOR, mac3, 1, NodeType::SENSOR);

    auto peers = pm.get_all();
    // The last peer add must be first on vector list
    TEST_ASSERT_EQUAL(NodeId::SOLAR_SENSOR, peers[0].node_id);

    // Re adding HUB should move it to the front
    pm.add(NodeId::HUB, mac1, 1, NodeType::HUB);

    peers = pm.get_all();
    // HUB must be the first
    TEST_ASSERT_EQUAL(NodeId::HUB, peers[0].node_id);
}

TEST_CASE("PeerManager updates peer channel", "[peer_manager]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Add a peer with channel 1
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    pm.add(NodeId::WATER_TANK, mac, 1, NodeType::SENSOR);

    // Change channel to 6 but same MAC
    pm.add(NodeId::WATER_TANK, mac, 6, NodeType::SENSOR);

    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(1, peers.size());
    TEST_ASSERT_EQUAL(6, peers[0].channel); // Must be channel 6
}

TEST_CASE("PeerManager handles MAC update failure gracefully", "[peer_manager]")
{
    // First call with ESP_OK
    esp_now_add_peer_ExpectAndReturn(nullptr, ESP_OK);
    esp_now_add_peer_IgnoreArg_peer();
    // esp_now_add_peer_IgnoreArg_peer_info();

    // Second call with ESP_FAIL
    esp_now_add_peer_ExpectAndReturn(nullptr, ESP_FAIL);
    esp_now_add_peer_IgnoreArg_peer();
    // esp_now_add_peer_IgnoreArg_peer_info();

    MockStorage storage;
    RealPeerManager pm(storage);

    // First add with ESP_OK return
    uint8_t mac_old[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    pm.add(NodeId::WATER_TANK, mac_old, 1, NodeType::SENSOR); // OK

    // Second add with ESP_FAIL simulating internal espnow criver error
    uint8_t mac_new[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    esp_err_t err      = pm.add(NodeId::WATER_TANK, mac_new, 1, NodeType::SENSOR); // FAIL

    TEST_ASSERT_EQUAL(ESP_FAIL, err);

    // Old peer must exist and MAC must be the old one
    uint8_t found_mac[6];
    TEST_ASSERT_TRUE(pm.find_mac(NodeId::WATER_TANK, found_mac));
    TEST_ASSERT_EQUAL_MEMORY(mac_old, found_mac, 6);
}

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
    esp_restart();
}