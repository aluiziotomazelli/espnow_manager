// test_apps/test_multiple_devices/main/test_multiple_devices.cpp
//
// On-target multi-device tests for EspNowManager.
// Requires two physical devices connected via UART to a host computer.
// Run with: idf.py -C test_apps/test_multiple_devices flash monitor
//
// Device roles are determined by test execution order:
//   DUT1 = HUB
//   DUT2 = NODE
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "unity.h"
#include "test_utils.h"
#include "nvs_flash.h"

#include "espnow_manager.hpp"

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr uint8_t kHubChannel = 1;
static constexpr uint8_t kNodeChannel = 2;
static constexpr uint32_t kPairingTimeoutMs = 5000;
static constexpr uint32_t kHeartbeatIntervalMs = 2000; // short for testing
static constexpr uint32_t kWaitAfterPairingMs = 3000;  // time for pairing to complete
static constexpr uint32_t kAppQueueLength = 10;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a minimal EspNowConfig for HUB role
static EspNowConfig make_hub_config(QueueHandle_t app_queue)
{
    EspNowConfig cfg{};
    cfg.node_id = ReservedIds::HUB;
    cfg.node_type = ReservedTypes::HUB;
    cfg.wifi_channel = kHubChannel;
    cfg.app_rx_queue = app_queue;
    cfg.heartbeat_interval_ms = kHeartbeatIntervalMs;
    cfg.stack_size_rx_task = 7168;
    return cfg;
}

// Create a minimal EspNowConfig for NODE role
static EspNowConfig make_node_config(QueueHandle_t app_queue)
{
    EspNowConfig cfg{};
    cfg.node_id = 0x02;
    cfg.node_type = 0x02;
    cfg.wifi_channel = kNodeChannel;
    cfg.app_rx_queue = app_queue;
    cfg.heartbeat_interval_ms = kHeartbeatIntervalMs;
    cfg.stack_size_rx_task = 7168;
    // cfg.stack_size_tx_task = 2120;
    return cfg;
}

// ===========================================================================
// TEST: Pairing — HUB accepts a NODE and both reach OPERATIONAL state
//
// DUT1 (HUB):  initializes, starts pairing, waits for node to pair
// DUT2 (NODE): initializes, starts pairing, waits for hub acceptance
// ===========================================================================

TEST_CASE("Clear NVS and peer storage", "[espnow][setup]")
{
    // Nuclear option — apaga tudo
    nvs_flash_erase();
    nvs_flash_init();
    printf("NVS Flash Ereased");
}

static void hub_pairing_test()
{
    QueueHandle_t app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(app_queue);

    EspNowManager &mgr = EspNowManager::instance();
    mgr.init(make_hub_config(app_queue));
    TEST_ASSERT_EQUAL(NodeState::PAIRING, mgr.get_node_state()); // HUB has no peers, starts PAIRING

    // Signal node that hub is ready to pair
    mgr.start_pairing(kPairingTimeoutMs);
    unity_send_signal("hub pairing started");

    // Wait for node to complete pairing
    unity_wait_for_signal("node paired");

    // HUB should now have the node as a peer
    TEST_ASSERT_EQUAL(1, mgr.get_peers().size());

    mgr.deinit();
    vQueueDelete(app_queue);
}

static void node_pairing_test()
{
    QueueHandle_t app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(app_queue);

    EspNowManager &mgr = EspNowManager::instance();
    mgr.init(make_node_config(app_queue));
    TEST_ASSERT_EQUAL(NodeState::PAIRING, mgr.get_node_state()); // No peers, starts PAIRING

    // Wait for hub to be ready
    unity_wait_for_signal("hub pairing started");

    // Node starts pairing — will scan for hub and send pair request
    mgr.start_pairing(kPairingTimeoutMs);

    // Give pairing time to complete
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));

    // Node should have transitioned to OPERATIONAL after hub accepted
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, mgr.get_node_state());

    // Hub should be in node's peer list
    TEST_ASSERT_EQUAL(1, mgr.get_peers().size());

    unity_send_signal("node paired");

    mgr.deinit();
    vQueueDelete(app_queue);
}

TEST_CASE_MULTIPLE_DEVICES(
    "Pairing: HUB accepts NODE and both reach OPERATIONAL",
    "[espnow][pairing]",
    hub_pairing_test,
    node_pairing_test);

// ===========================================================================
// TEST: Heartbeat — NODE sends heartbeat, HUB updates peer last_seen
//
// Requires pairing to complete first, then verifies heartbeat flow.
// DUT1 (HUB):  waits for heartbeat, checks get_offline_peers() is empty
// DUT2 (NODE): pairs and lets heartbeat timer fire naturally
// ===========================================================================

static void hub_heartbeat_test()
{
    QueueHandle_t app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(app_queue);

    EspNowManager &mgr = EspNowManager::instance();
    mgr.init(make_hub_config(app_queue));

    // mgr.start_pairing(kPairingTimeoutMs);
    unity_send_signal("hub ready for heartbeat test");

    // Wait for at least one heartbeat interval
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs * 2));

    // Node should not be considered offline — heartbeat was received
    auto offline = mgr.get_offline_peers();
    TEST_ASSERT_EQUAL(0, offline.size());

    unity_send_signal("hub heartbeat verified");
    mgr.deinit();
    vQueueDelete(app_queue);
}

static void node_heartbeat_test()
{
    QueueHandle_t app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(app_queue);

    EspNowManager &mgr = EspNowManager::instance();
    mgr.init(make_node_config(app_queue));

    unity_wait_for_signal("hub ready for heartbeat test");

    // mgr.start_pairing(kPairingTimeoutMs);
    // vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, mgr.get_node_state());

    // Wait for hub to verify heartbeat
    unity_wait_for_signal("hub heartbeat verified");

    mgr.deinit();
    vQueueDelete(app_queue);
}

TEST_CASE_MULTIPLE_DEVICES(
    "Heartbeat: NODE sends heartbeat, HUB marks peer as online",
    "[espnow][heartbeat]",
    hub_heartbeat_test,
    node_heartbeat_test);

// ===========================================================================
// TEST: send_data — HUB sends DATA to NODE, NODE receives on app_queue
//
// DUT1 (HUB):  pairs, sends DATA packet to node
// DUT2 (NODE): pairs, waits for DATA on app_queue, verifies sender_id
// ===========================================================================

static void hub_send_data_test()
{
    QueueHandle_t app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(app_queue);

    EspNowManager &mgr = EspNowManager::instance();
    TEST_ASSERT_EQUAL(ESP_OK, mgr.init(make_hub_config(app_queue)));

    // mgr.start_pairing(kPairingTimeoutMs);
    unity_send_signal("hub ready for data test");
    unity_wait_for_signal("node ready for data");

    // Send a small payload to the node
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    esp_err_t ret = mgr.send_data(0x02, static_cast<PayloadType>(0x01), payload, sizeof(payload));
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    unity_wait_for_signal("node received data");
    mgr.deinit();
    vQueueDelete(app_queue);
}

static void node_receive_data_test()
{
    QueueHandle_t app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(app_queue);

    EspNowManager &mgr = EspNowManager::instance();
    TEST_ASSERT_EQUAL(ESP_OK, mgr.init(make_node_config(app_queue)));

    unity_wait_for_signal("hub ready for data test");

    // mgr.start_pairing(kPairingTimeoutMs);
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, mgr.get_node_state());

    unity_send_signal("node ready for data");

    // Wait for DATA packet on app_queue
    AppMessage msg{};
    BaseType_t received = xQueueReceive(app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, received);

    // Verify sender is the HUB
    TEST_ASSERT_EQUAL(ReservedIds::HUB, msg.sender_id);
    TEST_ASSERT_EQUAL(static_cast<PayloadType>(0x01), msg.payload_type);
    TEST_ASSERT_EQUAL(4, msg.payload_len);

    unity_send_signal("node received data");
    mgr.deinit();
    vQueueDelete(app_queue);
}

TEST_CASE_MULTIPLE_DEVICES(
    "send_data: HUB sends DATA to NODE, NODE receives on app_queue",
    "[espnow][data]",
    hub_send_data_test,
    node_receive_data_test);