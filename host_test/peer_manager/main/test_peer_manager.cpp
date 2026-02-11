#include "peer_manager.hpp"
#include "mock_storage.hpp"
#include "unity.h"
#include "esp_system.h"
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
    esp_err_t err = pm.add(NodeId::WATER_TANK, mac1, 1, NodeType::SENSOR);
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

    for (int i = 0; i < MAX_PEERS; ++i)
    {
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

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
    esp_restart();
}
