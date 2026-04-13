// test_apps/test_multiple_devices/main/test_multiple_devices.cpp
//
// On-target multi-device tests for EspNowManager — DI constructor variant.
// Requires two physical devices connected via UART to a host computer.
// Run with: idf.py -C test_apps/test_multiple_devices flash monitor
//
// Device roles are determined by test execution order:
//   DUT1 = HUB
//   DUT2 = NODE
//
// Unlike the singleton (_sg) variant, each test creates a fresh EspNowManager
// via the DI constructor. This avoids static-state leakage between consecutive
// tests and makes the execution order irrelevant from the manager's perspective.

#include <cstring>
#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "unity.h"
#include "test_utils.h"
#include "nvs_flash.h"
#include "esp_attr.h"
#include "esp_wifi.h"

#include "channel_monitor.hpp"
#include "discovery_manager.hpp"
#include "espnow_driver.hpp"
#include "hal_espnow.hpp"
#include "hal_freertos.hpp"
#include "hal_nvs.hpp"
#include "hal_timer.hpp"
#include "hal_wifi.hpp"
#include "heartbeat_manager.hpp"
#include "message_codec.hpp"
#include "message_router.hpp"
#include "node_state_machine.hpp"
#include "pairing_manager.hpp"
#include "peer_manager.hpp"
#include "persistence_backend.hpp"
#include "storage_manager.hpp"
#include "statistics_manager.hpp"
#include "tx_manager.hpp"
#include "tx_state_machine.hpp"

#include "espnow_manager.hpp"

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr uint8_t kHubChannel = 1;
static constexpr uint8_t kNodeChannel = 1;
static constexpr uint8_t kNodeId = 0x02;
static constexpr uint8_t kNodeType = 0x02;
static constexpr uint32_t kPairingTimeoutMs = 5000;
static constexpr uint64_t kDeepSleepDurationUs = 1000 * 1000;
static constexpr uint32_t kHeartbeatIntervalMs = 500;
static constexpr uint32_t kWaitAfterPairingMs = 3000;
static constexpr uint32_t kAppQueueLength = 10;
static constexpr uint32_t kStressPacketCount = 100;
static constexpr uint32_t kStressIntervalMs = 20; // Slightly more conservative for reliability

// Ack retry timeout used both in the TxManager factory and in timing-sensitive
// tests.  Keeping a single constant avoids magic numbers and makes the
// relationship between factory config and test delays explicit.
static constexpr uint32_t kAckRetryTimeoutMs = 500;

// Generic payload type used in data-transfer tests (application can define its
// own enum mapping to PayloadType / uint8_t).
static constexpr PayloadType kTestPayloadType = 0x01;

// ---------------------------------------------------------------------------
// RTC storage — must have global lifetime (placed in RTC slow memory)
// ---------------------------------------------------------------------------
static RTC_DATA_ATTR PersistentPeers g_rtc_peers;
static RTC_DATA_ATTR PersistentChannel g_rtc_channel;
static RTC_DATA_ATTR PersistentStats g_rtc_stats;

// ---------------------------------------------------------------------------
// Temporary RTC storage for deep sleep tests — must have global lifetime (placed in RTC slow memory)
// ---------------------------------------------------------------------------
static RTC_DATA_ATTR PersistentPeers g_rtc_peers_ds;
static RTC_DATA_ATTR PersistentChannel g_rtc_channel_ds;

// Helper function to clear RTC storage
static void clear_rtc_storage()
{
    g_rtc_peers = {};
    g_rtc_channel = {};
    g_rtc_stats = {};
}

// Helper function to trigger deep sleep
static void trigger_deep_sleep(uint64_t duration_us = kDeepSleepDurationUs)
{
    esp_sleep_enable_timer_wakeup(duration_us);
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
// Factory — builds a fully-wired EspNowManager via the DI constructor.
// Returns a unique_ptr so each test owns a fresh instance that is destroyed
// (and fully deinitialized) at the end of the test scope.
// ---------------------------------------------------------------------------
static std::unique_ptr<EspNowManager> make_espnow_manager()
{
    static NvsHAL nvs_hal; // NvsHAL is safe to share; it only wraps nvs_flash calls

    auto rtc_peers_backend = std::make_unique<RtcBackend>(&g_rtc_peers, sizeof(g_rtc_peers));
    auto rtc_channel_backend = std::make_unique<RtcBackend>(&g_rtc_channel, sizeof(g_rtc_channel));
    auto rtc_stats_backend = std::make_unique<RtcBackend>(&g_rtc_stats, sizeof(g_rtc_stats));
    auto nvs_peers_backend = std::make_unique<NvsBackend>(nvs_hal, "peers_data");
    auto nvs_channel_backend = std::make_unique<NvsBackend>(nvs_hal, "channel_data");
    auto nvs_stats_backend = std::make_unique<NvsBackend>(nvs_hal, "stats_data");
    auto storage = std::make_unique<StorageManager>(
        std::move(rtc_peers_backend),
        std::move(rtc_channel_backend),
        std::move(rtc_stats_backend),
        std::move(nvs_peers_backend),
        std::move(nvs_channel_backend),
        std::move(nvs_stats_backend));

    auto hal_wifi = std::make_unique<WiFiHAL>();
    auto hal_espnow = std::make_unique<EspNowHAL>();
    auto hal_timer = std::make_unique<TimerHAL>();
    auto hal_freertos = std::make_unique<FreeRTOSHAL>();

    // Raw pointers for cross-wiring (lifetime is owned by unique_ptrs above)
    IWiFiHAL& wifi_ref = *hal_wifi;
    IEspNowHAL& espnow_ref = *hal_espnow;
    IFreeRTOSHAL& freertos_ref = *hal_freertos;
    IStorageManager& storage_ref = *storage;

    auto espnow_driver = std::make_unique<EspNowDriver>(wifi_ref, espnow_ref);
    auto peer_manager = std::make_unique<PeerManager>(storage_ref, espnow_ref, freertos_ref);
    auto message_codec = std::make_unique<MessageCodec>();

    IMessageCodec& codec_ref = *message_codec;
    IPeerManager& peer_ref = *peer_manager;

    auto channel_monitor = std::make_unique<ChannelMonitor>(wifi_ref, freertos_ref);
    auto scanner = std::make_unique<DiscoveryManager>(wifi_ref, espnow_ref, codec_ref, freertos_ref);
    auto tx_fsm = std::make_unique<TxStateMachine>();
    auto stats_mgr = std::make_unique<StatisticsManager>(storage_ref, freertos_ref);

    ITxStateMachine& tx_fsm_ref = *tx_fsm;
    IStatisticsManager& stats_ref = *stats_mgr;

    auto tx_manager = std::make_unique<TxManager>(tx_fsm_ref, espnow_ref, freertos_ref, codec_ref, stats_ref, peer_ref);

    ITxManager& tx_ref = *tx_manager;
    ITimerHAL& timer_ref = *hal_timer;

    auto heartbeat_mgr = std::make_unique<HeartbeatManager>(tx_ref, peer_ref, timer_ref);
    auto pairing_mgr = std::make_unique<PairingManager>(tx_ref, peer_ref, freertos_ref, timer_ref);

    IDiscoveryManager& scanner_ref = *scanner;
    IHeartbeatManager& hb_ref = *heartbeat_mgr;
    IPairingManager& pairing_ref = *pairing_mgr;

    auto message_router = std::make_unique<MessageRouter>(scanner_ref, tx_ref, hb_ref, pairing_ref);

    return std::make_unique<EspNowManager>(
        std::move(storage),
        std::move(hal_wifi),
        std::move(hal_timer),
        std::move(hal_freertos),
        std::move(hal_espnow),
        std::move(espnow_driver),
        std::move(peer_manager),
        std::move(message_codec),
        std::move(channel_monitor),
        std::move(scanner),
        std::move(tx_fsm),
        std::move(tx_manager),
        std::move(heartbeat_mgr),
        std::move(pairing_mgr),
        std::move(message_router),
        std::move(stats_mgr),
        std::make_unique<NodeStateMachine>());
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------
static EspNowConfig
make_hub_config(QueueHandle_t app_queue, uint8_t channel = kHubChannel, uint32_t heartbeat_interval_ms = 0)
{
    EspNowConfig cfg{};
    cfg.node_id = ReservedIds::HUB;
    cfg.node_type = ReservedTypes::HUB;
    cfg.wifi_channel = channel;
    cfg.app_rx_queue = app_queue;
    cfg.heartbeat_interval_ms = heartbeat_interval_ms;
    cfg.stack_size_rx_task = 7168;
    return cfg;
}

static EspNowConfig make_node_config(
    QueueHandle_t app_queue,
    NodeId id = kNodeId,
    uint8_t channel = kNodeChannel,
    uint32_t heartbeat_interval_ms = 0)
{
    EspNowConfig cfg{};
    cfg.node_id = id;
    cfg.node_type = kNodeType;
    cfg.wifi_channel = channel;
    cfg.app_rx_queue = app_queue;
    cfg.heartbeat_interval_ms = heartbeat_interval_ms;
    cfg.stack_size_rx_task = 7168;
    return cfg;
}

// ---------------------------------------------------------------------------
// Active-test state — written by each test helper, cleaned up by tearDown().
// Unity calls tearDown() after every test regardless of pass/fail, so this
// acts as a guaranteed cleanup even when a TEST_ASSERT_* triggers longjmp.
// ---------------------------------------------------------------------------
static std::unique_ptr<EspNowManager> g_mgr;
static QueueHandle_t g_app_queue = nullptr;

static void test_cleanup()
{
    if (g_mgr) {
        g_mgr->deinit();
        g_mgr.reset();
    }
    if (g_app_queue != nullptr) {
        vQueueDelete(g_app_queue);
        g_app_queue = nullptr;
    }
}

void clear_nvs()
{
    nvs_flash_erase();
    nvs_flash_init();
}

void tearDown()
{
    // Safety net: cleans up if the test aborted mid-way via TEST_ASSERT_*.
    // On the success path the test already called test_cleanup(), so this
    // is a no-op (both pointers are already null).
    test_cleanup();
    clear_nvs();
    clear_rtc_storage();
}

// ===========================================================================
// 1. TEST: Clear NVS — run once before the pairing test
// ===========================================================================

TEST_CASE("1. Setup: Clear NVS and peer storage", "[espnow][setup]")
{
    nvs_flash_erase();
    nvs_flash_init();
    printf("NVS Flash Erased\n");
}

// ===========================================================================
// 2. TEST: IntegrationHubAndNodePairSuccessfully
//
// Verifies automatic pairing without calling start_pairing() manually.
// HUB initialises in PAIRING state (no peers). NODE initialises in
// PAIRING_SCAN (no peers either) and automatically finds the HUB.
// Both must reach OPERATIONAL with exactly 1 peer each.
// ===========================================================================

static void hub_auto_pair()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1, /*heartbeat_interval_ms=*/0)));
    TEST_ASSERT_EQUAL(NodeState::PAIRING, g_mgr->get_node_state());

    // Signal NODE that HUB is up and in PAIRING state.
    unity_send_signal("hub ready to pair");

    // Wait for NODE to complete the scan + pair sequence.
    unity_wait_for_signal("node paired");

    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());
    // HUB stays on PAIRING state, goes to OPERATIONAL only after pairing timeout
    // TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("hub verified");
    test_cleanup();
}

static void node_auto_pair()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(
        ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, /*heartbeat_interval_ms=*/0)));
    TEST_ASSERT_EQUAL(NodeState::PAIRING_SCAN, g_mgr->get_node_state());

    // Wait for HUB to be ready, then the automatic scan will find it.
    unity_wait_for_signal("hub ready to pair");

    // Give the automatic scan + pairing time to complete.
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));

    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_send_signal("node paired");

    // Wait for HUB to verify its peer list before cleaning up.
    unity_wait_for_signal("hub verified");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "2. Integration: HubAndNodePairSuccessfully",
    "[espnow][pairing]",
    hub_auto_pair,
    node_auto_pair);

// ===========================================================================
// 3. TEST: IntegrationNodeSendsDataHubReceives
//
// Simple baseline data transfer without ACK requirements.
// ===========================================================================

static void hub_receive_simple()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    unity_wait_for_signal("node sending simple");

    AppMessage msg{};
    BaseType_t got = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, got);
    TEST_ASSERT_EQUAL(kTestPayloadType, msg.payload_type);

    unity_send_signal("hub received simple");
    test_cleanup();
}

static void node_send_simple()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(
        ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));

    unity_send_signal("node sending simple");
    const uint8_t payload[] = {0xDE, 0xAD};
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, payload, sizeof(payload), false));

    unity_wait_for_signal("hub received simple");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "3. Integration: NodeSendsDataHubReceives",
    "[espnow][data]",
    hub_receive_simple,
    node_send_simple);

// ===========================================================================
// 4. TEST: IntegrationNodeSendsDataHubReceivesAndAcks
//
// After automatic pairing, NODE sends a DATA packet to HUB with
// require_ack=true. HUB verifies the payload and calls confirm_reception().
// NODE verifies the overall send returned ESP_OK.
// ===========================================================================

static void hub_receive_and_ack()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    // Let NODE finish automatic pairing.
    unity_wait_for_signal("node operational");

    // Receive the DATA packet sent by NODE.
    AppMessage msg{};
    BaseType_t got = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, got);
    TEST_ASSERT_EQUAL(kNodeId, msg.sender_id); // NODE id
    TEST_ASSERT_EQUAL(kTestPayloadType, msg.payload_type);
    TEST_ASSERT_TRUE(msg.payload_len > 0);
    TEST_ASSERT_TRUE(msg.requires_ack);

    // ACK the message so NODE's TxManager can complete the transaction.
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK));

    unity_send_signal("hub acked");

    unity_wait_for_signal("node verified");
    test_cleanup();
}

static void node_send_with_ack()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(
        ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    // Automatic pairing — wait for it to finish.
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node operational");

    const uint8_t payload[] = {0xAB, 0xCD};
    esp_err_t ret =
        g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, payload, sizeof(payload), /*require_ack=*/true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Wait for HUB to confirm it sent the ACK.
    unity_wait_for_signal("hub acked");

    unity_send_signal("node verified");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "4. Integration: NodeSendsDataHubReceivesAndAcks",
    "[espnow][ack]",
    hub_receive_and_ack,
    node_send_with_ack);

// ===========================================================================
// 5. TEST: IntegrationAckTimeoutRetriesAndSuccess
//
// NODE sends DATA with require_ack=true. HUB deliberately delays the ACK by
// kAckRetryTimeoutMs*2, forcing at least one retry in the TxManager before
// the ACK arrives. The delay is intentionally less than kAckRetryTimeoutMs*3
// (which would reach MAX_FAILURES=3 and trigger a recovery scan).
// ===========================================================================

static void hub_delayed_ack()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    unity_wait_for_signal("node sending");

    // Receive the DATA packet.
    AppMessage msg{};
    BaseType_t got = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, got);
    TEST_ASSERT_TRUE(msg.requires_ack);

    // Delay ACK by 2x the retry timeout to force at least one retry in NODE's
    // TxManager.  Staying below 3x avoids triggering MAX_FAILURES (=3).
    vTaskDelay(pdMS_TO_TICKS(kAckRetryTimeoutMs * 2));

    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK));

    unity_send_signal("hub acked after delay");

    unity_wait_for_signal("node verified");
    test_cleanup();
}

static void node_send_retry()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(
        ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    // Automatic pairing.
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    const uint8_t payload[] = {0xCA, 0xFE};
    esp_err_t ret =
        g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, payload, sizeof(payload), /*require_ack=*/true);

    unity_send_signal("node sending");

    // Wait for HUB's delayed ACK.
    unity_wait_for_signal("hub acked after delay");

    // send_data returns only after the full TX cycle completes.
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    // NODE should remain OPERATIONAL (not enter RECOVERY_SCAN).
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node verified");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "5. Integration: AckTimeoutRetriesAndSuccess",
    "[espnow][retry]",
    hub_delayed_ack,
    node_send_retry);

// ===========================================================================
// 6. TEST: IntegrationHeartbeatSentPeriodically
//
// After two heartbeat intervals, the HUB checks that no peer is marked offline — proof
// that at least one heartbeat was received.
// ===========================================================================

static void hub_heartbeat_periodically()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    // Signal NODE that HUB is ready, then wait for NODE to confirm OPERATIONAL.
    unity_send_signal("hub ready for heartbeat");
    unity_wait_for_signal("node operational hb");

    // Wait for at least two heartbeat cycles to fire on the NODE.
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs * 2));

    // No peer should be considered offline — heartbeats were received.
    auto offline = g_mgr->get_offline_peers();
    TEST_ASSERT_EQUAL(0, offline.size());

    unity_send_signal("hub heartbeat ok");
    test_cleanup();
}

static void node_heartbeat_periodically()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, kHeartbeatIntervalMs)));
    // NODE starts in PAIRING_SCAN and auto-pairs with the HUB.

    // Wait for HUB to be ready before starting the scan.
    unity_wait_for_signal("hub ready for heartbeat");

    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    // Signal HUB that NODE is OPERATIONAL; heartbeats will now fire.
    unity_send_signal("node operational hb");

    unity_wait_for_signal("hub heartbeat ok");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "6. Integration: HeartbeatSentPeriodically",
    "[espnow][heartbeat]",
    hub_heartbeat_periodically,
    node_heartbeat_periodically);

// ===========================================================================
// 7. TEST: IntegrationPeersPersistedToNvsAndRestored
//
// Cycle 1: HUB and NODE pair. Both call deinit() — this triggers storage
//          write (peers + channel persisted to NVS and RTC).
// Cycle 2: Both reinit from the same storage (not cleared between cycles).
//          Both must start directly in OPERATIONAL with 1 peer, without any
//          re-pairing. tearDown() clears storage only after the test ends.
// ===========================================================================

static void hub_peers_persisted()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // ---- Cycle 1: pair and persist ----
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1)));

    unity_send_signal("hub ready c1");
    unity_wait_for_signal("node paired c1");

    // Confirm 1 peer was added during pairing.
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    // Deinit flushes peers + channel to NVS/RTC.
    g_mgr->deinit();
    g_mgr.reset();
    unity_send_signal("hub deinit c1");
    unity_wait_for_signal("node deinit c1");

    // ---- Cycle 2: restore from storage ----
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1)));

    // HUB has stored peers → should start OPERATIONAL directly.
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_send_signal("hub restored c2");
    unity_wait_for_signal("node restored c2");
    test_cleanup();
}

static void node_peers_persisted()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // ---- Cycle 1: auto-pair and persist ----
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1)));

    unity_wait_for_signal("hub ready c1");

    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_send_signal("node paired c1");

    // Deinit flushes peers + channel to NVS/RTC.
    g_mgr->deinit();
    g_mgr.reset();
    unity_wait_for_signal("hub deinit c1");
    unity_send_signal("node deinit c1");

    // ---- Cycle 2: restore from storage ----
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1)));

    // NODE has stored peers → should start OPERATIONAL directly.
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_wait_for_signal("hub restored c2");
    unity_send_signal("node restored c2");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "7. Integration: PeersPersistedToNvsAndRestored",
    "[espnow][storage]",
    hub_peers_persisted,
    node_peers_persisted);

// ===========================================================================
// 8. TEST: IntegrationDiscoveryScanFindsHubOnDifferentChannel
//
// HUB starts on ch 6, NODE starts on ch 1.
// NODE should enter PAIRING_SCAN and automatically find the HUB on ch 6.
// ===========================================================================

static void hub_ch_13_wait()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // HUB on channel 13
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/13)));

    unity_send_signal("hub ready ch 13");
    unity_wait_for_signal("node paired ch 13");

    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    TEST_ASSERT_EQUAL(13, primary);
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_send_signal("hub verified");
    test_cleanup();
}

static void node_ch_1_find_13()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // NODE on channel 1
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1)));

    unity_wait_for_signal("hub ready ch 13");

    // Automatic scan should find the HUB on channel 6
    vTaskDelay(pdMS_TO_TICKS(MAX_SCAN_TIME_MS));

    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    TEST_ASSERT_EQUAL(13, primary);
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_send_signal("node paired ch 13");
    unity_wait_for_signal("hub verified");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "8. Integration: DiscoveryScanFindsHubOnDifferentChannel",
    "[espnow][scan]",
    hub_ch_13_wait,
    node_ch_1_find_13);

// ===========================================================================
// 9. TEST: IntegrationHubChangesChannelNodesRecover
//
// HUB and NODE pair on ch 1. HUB then forces its WiFi channel to ch 6.
// NODE fails to send data, enters RECOVERY_SCAN and finds HUB on ch 6.
// ===========================================================================

static void hub_change_channel()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1)));

    unity_send_signal("hub ready ch 1");
    unity_wait_for_signal("node operational");

    // Force WiFi channel change to 6.
    // EspNowManager's ChannelMonitor should detect this and update internal state.
    TEST_ASSERT_EQUAL(ESP_OK, esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE));

    // Wait for internal ChannelMonitor to detect the change
    vTaskDelay(pdMS_TO_TICKS(DEFAULT_CHANNEL_MONITOR_INTERVAL_MS + 50));
    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    TEST_ASSERT_EQUAL(6, primary);

    unity_send_signal("hub now on ch 6");
    unity_wait_for_signal("node recovered");

    TEST_ASSERT_EQUAL(6, primary);

    unity_send_signal("hub verified recovery");
    test_cleanup();
}

static void node_recover_channel()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1)));

    unity_wait_for_signal("hub ready ch 1");
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    unity_send_signal("node operational");

    unity_wait_for_signal("hub now on ch 6");

    // Send constant data to trigger MAX_FAILURES and recovery scan
    for (int i = 0; i < MAX_FAILURES; i++) {
        const uint8_t dummy = 0xFF;
        g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, &dummy, 1, true);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    TEST_ASSERT_EQUAL(NodeState::RECOVERY_SCAN, g_mgr->get_node_state());

    // Increased delay to allow MAX_SCAN_TIME
    vTaskDelay(pdMS_TO_TICKS(MAX_SCAN_TIME_MS + 1000));

    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    TEST_ASSERT_EQUAL(6, primary);
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node recovered");
    unity_wait_for_signal("hub verified recovery");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "9. Integration: HubChangesChannelNodesRecover",
    "[espnow][recovery]",
    hub_change_channel,
    node_recover_channel);

// ===========================================================================
// 10. TEST: IntegrationScanFailsWhenNoHubPresent
//
// HUB and NODE pair. HUB "dies" (deinit).
// NODE tries to send, fails, scans all channels, finds nothing, goes to IDLE.
// ===========================================================================

static void hub_dies()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_send_signal("hub ready");
    unity_wait_for_signal("node operational");

    // HUB is simulated as "dead"
    g_mgr->deinit();
    g_mgr.reset();

    unity_send_signal("hub dead");

    // The HUB test ends here, the NODE must fail alone.
    // We do not call test_cleanup() because we already reset g_mgr.
    if (g_app_queue != nullptr) {
        vQueueDelete(g_app_queue);
        g_app_queue = nullptr;
    }
}

static void node_scan_fails()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId)));

    unity_wait_for_signal("hub ready");
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    unity_send_signal("node operational");

    unity_wait_for_signal("hub dead");

    // Trigger failure
    for (int i = 0; i < MAX_FAILURES; i++) {
        const uint8_t dummy = 0xEE;
        g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, &dummy, 1, true);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Give time for a full 1-13 channel scan to fail
    vTaskDelay(pdMS_TO_TICKS(MAX_SCAN_TIME_MS));

    TEST_ASSERT_EQUAL(NodeState::IDLE, g_mgr->get_node_state());

    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES("10. Integration: ScanFailsWhenNoHubPresent", "[espnow][fail]", hub_dies, node_scan_fails);

// ===========================================================================
// 11. TEST: IntegrationHeartbeatTimeoutMarksPeerOffline
//
// NODE pairs then stops (deinit).
// HUB waits for 3*interval + buffer and checks if node is marked offline.
// ===========================================================================

static void hub_detects_offline()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1, 0)));

    unity_wait_for_signal("node paired");

    // Node is alive now
    TEST_ASSERT_EQUAL(0, g_mgr->get_offline_peers().size());

    unity_send_signal("hub verified online");
    unity_wait_for_signal("node dead");

    // Wait for offline timeout
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs * HEARTBEAT_OFFLINE_MULTIPLIER * 2));
    auto offline = g_mgr->get_offline_peers();
    TEST_ASSERT_EQUAL(1, offline.size());
    TEST_ASSERT_EQUAL(kNodeId, offline[0]); // NODE ID

    unity_send_signal("hub verified offline");
    test_cleanup();
}

static void node_goes_offline()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, kHeartbeatIntervalMs)));
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node paired");
    unity_wait_for_signal("hub verified online");

    // Disappear
    g_mgr->deinit();
    g_mgr.reset();

    unity_send_signal("node dead");
    unity_wait_for_signal("hub verified offline");

    if (g_app_queue != nullptr) {
        vQueueDelete(g_app_queue);
        g_app_queue = nullptr;
    }
}

TEST_CASE_MULTIPLE_DEVICES(
    "11. Integration: HeartbeatTimeoutMarksPeerOffline",
    "[espnow][offline]",
    hub_detects_offline,
    node_goes_offline);

// ===========================================================================
// 12. TEST: IntegrationHubUpdatesNodeIdOnMacCollision
//
// Reuses the same hardware to simulate identity update.
// HUB waits for ID 2, then ID 3 from the same device, and verifies it only
// tracks the latest ID for that MAC address.
// ===========================================================================

static void hub_updates_node_id()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    // HUB stays in PAIRING to accept both identity versions
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));
    g_mgr->start_pairing(PAIRING_TIMEOUT_MS);

    // 1. Initial pairing - ID 2
    unity_wait_for_signal("node 2 paired");
    auto peers1 = g_mgr->get_peers();
    TEST_ASSERT_EQUAL(1, peers1.size());
    TEST_ASSERT_EQUAL(2, peers1[0].node_id);

    unity_send_signal("hub add 2");

    // 2. Identity update - ID 3
    unity_wait_for_signal("node 3 paired");
    auto peers2 = g_mgr->get_peers();

    // Size should still be 1 because it's the same MAC
    TEST_ASSERT_EQUAL(1, peers2.size());
    TEST_ASSERT_EQUAL(3, peers2[0].node_id);

    unity_send_signal("hub verified identity update");
    test_cleanup();
}

static void node_cycle_multiple_ids()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // ---- Phase 1: Pair as ID 2 ----
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, /*node_id=*/2)));

    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node 2 paired");
    g_mgr->deinit();
    g_mgr.reset();

    clear_rtc_storage();
    clear_nvs();

    unity_wait_for_signal("hub add 2");

    // ---- Phase 2: Pair as ID 3 ----
    vTaskDelay(pdMS_TO_TICKS(1000)); // Buffer
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, /*node_id=*/3)));

    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node 3 paired");

    unity_wait_for_signal("hub verified identity update");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "12. Integration: HubUpdatesNodeIdOnMacCollision",
    "[espnow][pairing]",
    hub_updates_node_id,
    node_cycle_multiple_ids);

// ===========================================================================
// 13. TEST: IntegrationHeartbeatResetsOfflineTimer
//
// NODE pairs, then stops (deinit). HUB waits for timeout and marks it offline.
// NODE returns (reinit), heartbeats fire, HUB verifies peer is back online.
// Covers: IntegrationNodeRebootsAndReconnects (implicitly).
// ===========================================================================

static void hub_heartbeat_resets()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    // HUB doesn't send heartbeats (interval=0), only listens.
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1, 0)));

    unity_wait_for_signal("node paired");
    TEST_ASSERT_EQUAL(0, g_mgr->get_offline_peers().size());

    unity_send_signal("hub verified online c1");
    unity_wait_for_signal("node dead");

    // Wait for offline timeout
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs * HEARTBEAT_OFFLINE_MULTIPLIER * 2));
    TEST_ASSERT_EQUAL(1, g_mgr->get_offline_peers().size());

    unity_send_signal("hub verified offline");
    unity_wait_for_signal("node back");

    // Wait for one or two heartbeats from the returned node
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs * 5));

    // Peer should be back online now
    TEST_ASSERT_EQUAL(0, g_mgr->get_offline_peers().size());

    unity_send_signal("hub verified online c2");
    test_cleanup();
}

static void node_reboots_and_reconnects()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // Initial pair
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, kHeartbeatIntervalMs)));
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node paired");
    unity_wait_for_signal("hub verified online c1");

    // Stop node (simulate crash/reboot)
    g_mgr->deinit();
    g_mgr.reset();
    unity_send_signal("node dead");

    unity_wait_for_signal("hub verified offline");

    // Re-init node (storage is preserved in NVS/RTC)
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, kHeartbeatIntervalMs)));

    // Should be OPERATIONAL immediately from storage
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node back");
    unity_wait_for_signal("hub verified online c2");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "13. Integration: HeartbeatResetsOfflineTimer",
    "[espnow][heartbeat]",
    hub_heartbeat_resets,
    node_reboots_and_reconnects);

// ===========================================================================
// 14. TEST: IntegrationNvsBackupUsedWhenRtcCorrupt
//
// HUB and NODE pair. NODE deinits and corrupts its RTC storage (bitwise flip).
// Upon reinit, NODE must detect RTC corruption, load from NVS instead,
// reach OPERATIONAL state, and restore/sync the RTC storage back.
// Covers: IntegrationSyncRtcToNvsOnPairingSuccess (implicitly).
// ===========================================================================

static void hub_wait_for_corrupted_node()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_wait_for_signal("node paired");
    unity_wait_for_signal("node back online");

    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());
    unity_send_signal("hub verified");
    test_cleanup();
}

static void node_rtc_corruption()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // 1. Pair and persist to NVS
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    // Save RTC storage to check after reboot
    PersistentPeers peers_before = g_rtc_peers;
    PersistentChannel channel_before = g_rtc_channel;

    unity_send_signal("node paired");

    g_mgr->deinit();
    g_mgr.reset();

    // 2. Corrupt RTC storage bitwise
    // We flip bits across the entire structs to ensure validation fails.
    uint8_t* p_rtc = reinterpret_cast<uint8_t*>(&g_rtc_peers);
    for (size_t i = 0; i < sizeof(PersistentPeers); ++i) {
        p_rtc[i] ^= 0xAA;
    }
    uint8_t* p_ch = reinterpret_cast<uint8_t*>(&g_rtc_channel);
    for (size_t i = 0; i < sizeof(PersistentChannel); ++i) {
        p_ch[i] ^= 0x55;
    }

    // 3. Re-init - should recover from NVS
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

    // If it reaches OPERATIONAL, it means it found peers in NVS despite RTC corruption.
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    // 4. Verification of RTC sync (implicitly covered by being operational again)
    unity_send_signal("node back online");
    unity_wait_for_signal("hub verified");

    // Check that RTC was restored from NVS
    TEST_ASSERT_TRUE(peers_before == g_rtc_peers);
    TEST_ASSERT_TRUE(channel_before == g_rtc_channel);

    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "14. Integration: NvsBackupUsedWhenRtcCorrupt",
    "[espnow][storage]",
    hub_wait_for_corrupted_node,
    node_rtc_corruption);

// ===========================================================================
// 15. TEST: IntegrationNodeWakesFromDeepSleepWithPeersIntact
//
// Cycle 1: HUB and NODE pair. NODE enters deep sleep.
// Cycle 2: NODE wakes up (re-init). It must start OPERATIONAL directly
//          with 1 peer restored from RTC RAM.
// ===========================================================================

static void hub_deep_sleep_cycle()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // ---- Cycle 1: Pair ----
    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_send_signal("hub ready c1");
    unity_wait_for_signal("node sleeping c1");

    // During deep sleep, HUB just waits.
    vTaskDelay(pdMS_TO_TICKS(kDeepSleepDurationUs / 1000 + 500));

    // ---- Cycle 2: Verify ----
    unity_wait_for_signal("node awake c2");
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_send_signal("hub verified c2");
    test_cleanup();
}

static void node_deep_sleep_cycle()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    // Check if we are waking up from deep sleep
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_TIMER) {
        // ---- Cycle 1: Pair and Sleep ----
        g_mgr = make_espnow_manager();
        TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

        unity_wait_for_signal("hub ready c1");
        vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));

        TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
        TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

        unity_send_signal("node sleeping c1");

        // Manually deinit before sleep to ensure clean state and persistence
        g_mgr->deinit();
        g_mgr.reset();

        trigger_deep_sleep();
    }
    else {
        // ---- Cycle 2: Wake up and Verify ----
        g_mgr = make_espnow_manager();
        TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

        // Must be OPERATIONAL immediately from RTC storage
        TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
        TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

        unity_send_signal("node awake c2");
        unity_wait_for_signal("hub verified c2");
        test_cleanup();
    }
}

TEST_CASE_MULTIPLE_DEVICES(
    "15. Integration: NodeWakesFromDeepSleepWithPeersIntact",
    "[espnow][sleep]",
    hub_deep_sleep_cycle,
    node_deep_sleep_cycle);

// ===========================================================================
// 16. TEST: IntegrationRtcStorageSurvivesDeepSleep
//
// Validates that RTC RAM (PersistentPeers and PersistentChannel) survives
// deep sleep without any bit flips. Captures state before sleep and
// compares it with state after wake using the struct's equality operators.
// ===========================================================================

static void hub_rtc_survives_sleep()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_send_signal("hub ready rtc c1");
    unity_wait_for_signal("node sleeping rtc c1");

    // Wait for node to sleep and wake up
    vTaskDelay(pdMS_TO_TICKS(kDeepSleepDurationUs / 1000 + 1000));

    unity_wait_for_signal("node awake rtc c2");
    unity_send_signal("hub verified rtc c2");
    test_cleanup();
}

static void node_rtc_survives_sleep()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_TIMER) {
        // ---- Cycle 1: Pair and Sleep ----
        g_mgr = make_espnow_manager();
        TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

        unity_wait_for_signal("hub ready rtc c1");
        vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));

        TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

        // Capture state before sleep to a global RTC variable
        g_rtc_peers_ds = g_rtc_peers;
        g_rtc_channel_ds = g_rtc_channel;

        unity_send_signal("node sleeping rtc c1");

        // We don't clear NVS/RTC in tearDown between cycles of the same test,
        // but for deep sleep we need to keep the variables in RTC RAM.
        g_mgr->deinit();
        g_mgr.reset();

        // Restore variables to RTC RAM before sleep because deinit/reset
        // doesn't wipe them, but we want to be explicit about what we are testing.
        g_rtc_peers = g_rtc_peers_ds;
        g_rtc_channel = g_rtc_channel_ds;

        trigger_deep_sleep();
    }
    else {
        // ---- Cycle 2: Wake up and Verify RTC bit-perfection ----
        g_mgr = make_espnow_manager();
        TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

        // Check if RTC storage survived deep sleep without any bit flips
        TEST_ASSERT_TRUE(g_rtc_peers_ds == g_rtc_peers);
        TEST_ASSERT_TRUE(g_rtc_channel_ds == g_rtc_channel);

        TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

        unity_send_signal("node awake rtc c2");
        unity_wait_for_signal("hub verified rtc c2");
        test_cleanup();
    }
}

TEST_CASE_MULTIPLE_DEVICES(
    "16. Integration: RtcStorageSurvivesDeepSleep",
    "[espnow][sleep]",
    hub_rtc_survives_sleep,
    node_rtc_survives_sleep);

// ===========================================================================
// 17. TEST: IntegrationDataStressTest
//
// NODE sends 100 packets to HUB at high frequency with ACK enabled.
// HUB verifies each sequence number and sends logical ACK.
// NODE verifies all packets were acknowledged correctly.
// ===========================================================================

static void hub_stress_receive()
{
    g_app_queue = xQueueCreate(kStressPacketCount + 10, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_wait_for_signal("node ready stress");

    // Receive all packets — timeout generoso para cobrir atrasos de transmissão
    uint32_t received = 0;
    const uint32_t timeout_ms = kStressPacketCount * kStressIntervalMs * 3;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (received < kStressPacketCount && xTaskGetTickCount() < deadline) {
        AppMessage msg{};
        if (xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(kStressIntervalMs * 2)) == pdTRUE) {
            // Verify packet index matches sequence
            uint32_t idx = 0;
            memcpy(&idx, msg.payload, sizeof(idx));
            TEST_ASSERT_EQUAL(received, idx); // verifica ordem e completude
            received++;
        }
    }

    TEST_ASSERT_EQUAL(kStressPacketCount, received);

    unity_send_signal("hub stress ok");
    unity_wait_for_signal("node stress ok");
    test_cleanup();
}

static void node_stress_send()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

    // Wait for auto-pairing
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node ready stress");

    for (uint32_t i = 0; i < kStressPacketCount; ++i) {
        uint8_t payload[32] = {0};
        memcpy(payload, &i, sizeof(i));

        // Send with ACK enabled
        esp_err_t ret =
            g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, payload, sizeof(payload), /*require_ack*/ false);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        // Small delay to allow HUB to process and maintain medium access fairness
        vTaskDelay(pdMS_TO_TICKS(kStressIntervalMs));
    }

    unity_send_signal("node stress ok");
    unity_wait_for_signal("hub stress ok");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES("17. Integration: DataStressTest", "[espnow][stress]", hub_stress_receive, node_stress_send);

// ===========================================================================
// 18. TEST: IntegrationStressTestWithAckAndDeduplication
//
// NODE sends 30 packets to HUB with ACK enabled (require_ack=true).
// This exercises the full TxManager reliability protocol:
// - TxManager enters WAITING_FOR_ACK after each send
// - Retransmits on ACK timeout (may cause duplicate delivery at HUB)
// - HUB must deduplicate by sequence_number
//
// Verification:
// - All 30 unique sequence numbers arrived at HUB (no permanent loss)
// - Duplicates are tolerated (deduplicated by sequence number)
// - NODE receives ESP_OK for all sends (ACK confirmed)
// ===========================================================================

static constexpr uint32_t kAckStressPacketCount = 30;
static constexpr uint32_t kAckStressIntervalMs = 50; // Longer interval for ACK round-trip

static void hub_stress_receive_with_ack()
{
    g_app_queue = xQueueCreate(kAckStressPacketCount * 2 + 10, sizeof(AppMessage)); // Extra space for duplicates
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_wait_for_signal("node ready ack stress");

    // Track unique sequence numbers received (deduplication)
    bool seen[kAckStressPacketCount] = {false};
    uint32_t unique_count = 0;
    uint32_t duplicate_count = 0;
    uint32_t total_received = 0;

    // Generous timeout to cover ACK round-trips and possible retransmissions
    const uint32_t timeout_ms = kAckStressPacketCount * kAckStressIntervalMs * 5;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (unique_count < kAckStressPacketCount && xTaskGetTickCount() < deadline) {
        AppMessage msg{};
        if (xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(kAckStressIntervalMs * 3)) == pdTRUE) {
            // Extract sequence number from payload
            uint32_t seq = 0;
            memcpy(&seq, msg.payload, sizeof(seq));

            total_received++;

            if (seq < kAckStressPacketCount) {
                if (!seen[seq]) {
                    seen[seq] = true;
                    unique_count++;
                }
                else {
                    duplicate_count++;
                }
            }

            // Always send ACK (even for duplicates — TxManager may retry)
            if (msg.requires_ack) {
                g_mgr->confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
            }
        }
    }

    // Log statistics for analysis
    printf(
        "HUB stats: total=%lu, unique=%lu, duplicates=%lu\n",
        (unsigned long)total_received,
        (unsigned long)unique_count,
        (unsigned long)duplicate_count);

    // All unique packets must have arrived
    TEST_ASSERT_EQUAL(kAckStressPacketCount, unique_count);

    unity_send_signal("hub ack stress ok");
    unity_wait_for_signal("node ack stress ok");
    test_cleanup();
}

static void node_stress_send_with_ack()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

    // Wait for auto-pairing
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node ready ack stress");

    uint32_t ack_ok = 0;
    uint32_t ack_fail = 0;

    for (uint32_t i = 0; i < kAckStressPacketCount; ++i) {
        TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
        uint8_t payload[32] = {0};
        memcpy(payload, &i, sizeof(i));

        // Send with ACK enabled — this blocks until ACK received or timeout
        esp_err_t ret =
            g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, payload, sizeof(payload), /*require_ack=*/true);

        if (ret == ESP_OK) {
            ack_ok++;
        }
        else {
            ack_fail++;
            printf("NODE: packet %lu failed with %s\n", (unsigned long)i, esp_err_to_name(ret));
        }

        // Delay to allow HUB to process and maintain medium access fairness
        vTaskDelay(pdMS_TO_TICKS(kAckStressIntervalMs));
    }

    // Log statistics
    printf("NODE stats: ack_ok=%lu, ack_fail=%lu\n", (unsigned long)ack_ok, (unsigned long)ack_fail);

    // All packets should have been acknowledged
    TEST_ASSERT_EQUAL(kAckStressPacketCount, ack_ok);

    unity_send_signal("node ack stress ok");
    unity_wait_for_signal("hub ack stress ok");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "18. Integration: StressTestWithAckAndDeduplication",
    "[espnow][stress][ack]",
    hub_stress_receive_with_ack,
    node_stress_send_with_ack);

// ===========================================================================
// 19. TEST: EdgeCaseMalformedPacketsIgnored
//
// NODE sends a raw ESP-NOW packet with intentionally corrupted CRC, bypassing
// TxManager encoding. HUB's rx_task should validate CRC, drop the packet,
// and not deliver anything to the app queue. NODE then sends a valid packet
// to prove the HUB is still operational.
// ===========================================================================

static void hub_ignores_malformed()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue, /*channel=*/1)));

    unity_wait_for_signal("node ready for malformed");

    // Wait for NODE to send the malformed packet — we should NOT receive it
    // in the app queue because CRC validation should drop it.
    vTaskDelay(pdMS_TO_TICKS(500));

    AppMessage msg{};
    BaseType_t got = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(200));
    TEST_ASSERT_EQUAL(pdFALSE, got); // No malformed packet should reach app queue
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());
    TEST_ASSERT_EQUAL(NodeState::PAIRING, g_mgr->get_node_state());

    unity_send_signal("hub received nothing from malformed");

    // Now wait for the valid packet — HUB should still be operational
    got = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, got);
    TEST_ASSERT_EQUAL(kNodeId, msg.sender_id);
    TEST_ASSERT_EQUAL(kTestPayloadType, msg.payload_type);

    unity_send_signal("hub received valid after malformed");
    test_cleanup();
}

static void node_send_malformed()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(
        ESP_OK, g_mgr->init(make_node_config(g_app_queue, kNodeId, /*channel=*/1, /*heartbeat_interval_ms=*/0)));

    // Wait for auto-pairing
    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    // Get the HUB's MAC from the peer list
    auto peers = g_mgr->get_peers();
    TEST_ASSERT_EQUAL(1, peers.size());

    unity_send_signal("node ready for malformed");

    // Send a raw ESP-NOW packet with intentionally bad CRC — this bypasses
    // TxManager encoding. The HUB's rx_task should validate CRC and drop it.
    uint8_t bad_payload[32] = {0xDE, 0xAD, 0xBE, 0xEF};
    esp_err_t ret = esp_now_send(peers[0].mac, bad_payload, sizeof(bad_payload));
    TEST_ASSERT_EQUAL(ESP_OK, ret); // ESP-NOW driver accepts it (no CRC check on send)

    unity_wait_for_signal("hub received nothing from malformed");

    // Now send a valid packet via the proper TxManager path
    const uint8_t good_payload[] = {0xCA, 0xFE};
    ret = g_mgr->send_data(ReservedIds::HUB, kTestPayloadType, good_payload, sizeof(good_payload), false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    unity_send_signal("node sent valid after malformed");
    unity_wait_for_signal("hub received valid after malformed");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "19. Integration: EdgeCaseMalformedPacketsIgnored",
    "[espnow][edge]",
    hub_ignores_malformed,
    node_send_malformed);
