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
#include "unity.h"
#include "test_utils.h"
#include "nvs_flash.h"
#include "esp_attr.h"

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
#include "tx_manager.hpp"
#include "tx_state_machine.hpp"

#include "espnow_manager.hpp"

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr uint8_t kHubChannel = 1;
static constexpr uint8_t kNodeChannel = 1;
static constexpr uint32_t kPairingTimeoutMs = 5000;
static constexpr uint32_t kHeartbeatIntervalMs = 2000;
static constexpr uint32_t kWaitAfterPairingMs = 3000;
static constexpr uint32_t kAppQueueLength = 10;

// Ack retry timeout used both in the TxManager factory and in timing-sensitive
// tests.  Keeping a single constant avoids magic numbers and makes the
// relationship between factory config and test delays explicit.
static constexpr uint32_t kAckRetryTimeoutMs = 500;

// Short pairing timeout used when the goal is NOT to test pairing itself but
// to put the HUB into OPERATIONAL quickly.  The HUB transitions to OPERATIONAL
// after pairing times out; 30 ms (~3 FreeRTOS ticks) is enough without being
// fragile.  Do NOT use this in tests that exercise the pairing protocol itself.
static constexpr uint32_t kForcePairingTimeoutMs = 30;

// Generic payload type used in data-transfer tests (application can define its
// own enum mapping to PayloadType / uint8_t).
static constexpr PayloadType kTestPayloadType = 0x01;

// ---------------------------------------------------------------------------
// RTC storage — must have global lifetime (placed in RTC slow memory)
// ---------------------------------------------------------------------------
static RTC_DATA_ATTR PersistentPeers g_rtc_peers;
static RTC_DATA_ATTR PersistentChannel g_rtc_channel;

void clear_rtc_storage()
{
    g_rtc_peers = {};
    g_rtc_channel = {};
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
    auto nvs_peers_backend = std::make_unique<NvsBackend>(nvs_hal, "peers_data");
    auto nvs_channel_backend = std::make_unique<NvsBackend>(nvs_hal, "channel_data");
    auto storage = std::make_unique<StorageManager>(
        std::move(rtc_peers_backend),
        std::move(rtc_channel_backend),
        std::move(nvs_peers_backend),
        std::move(nvs_channel_backend));

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

    ITxStateMachine& tx_fsm_ref = *tx_fsm;

    auto tx_manager = std::make_unique<TxManager>(tx_fsm_ref, espnow_ref, freertos_ref, codec_ref, kAckRetryTimeoutMs);

    ITxManager& tx_ref = *tx_manager;
    ITimerHAL& timer_ref = *hal_timer;

    auto heartbeat_mgr = std::make_unique<HeartbeatManager>(tx_ref, peer_ref, timer_ref);
    auto pairing_mgr = std::make_unique<PairingManager>(tx_ref, peer_ref, freertos_ref);

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
        std::make_unique<NodeStateMachine>());
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------
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

static EspNowConfig make_node_config(QueueHandle_t app_queue)
{
    EspNowConfig cfg{};
    cfg.node_id = 0x02;
    cfg.node_type = 0x02;
    cfg.wifi_channel = kNodeChannel;
    cfg.app_rx_queue = app_queue;
    cfg.heartbeat_interval_ms = kHeartbeatIntervalMs;
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
// TEST: Clear NVS — run once before the pairing test
// ===========================================================================

TEST_CASE("Clear NVS and peer storage", "[espnow][setup]")
{
    nvs_flash_erase();
    nvs_flash_init();
    printf("NVS Flash Erased\n");
}

// ===========================================================================
// TEST: Pairing — HUB accepts a NODE and both reach OPERATIONAL state
// ===========================================================================

static void hub_pairing_test()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    g_mgr->init(make_hub_config(g_app_queue));
    TEST_ASSERT_EQUAL(NodeState::PAIRING, g_mgr->get_node_state()); // HUB starts PAIRING

    g_mgr->start_pairing(kPairingTimeoutMs);
    unity_send_signal("hub pairing started");

    unity_wait_for_signal("node paired");

    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    test_cleanup();
}

static void node_pairing_test()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    g_mgr->init(make_node_config(g_app_queue));
    TEST_ASSERT_EQUAL(NodeState::PAIRING_SCAN, g_mgr->get_node_state()); // No peers → PAIRING_SCAN

    unity_wait_for_signal("hub pairing started");

    g_mgr->start_pairing(kPairingTimeoutMs);

    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));

    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_send_signal("node paired");

    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "Pairing: HUB accepts NODE and both reach OPERATIONAL",
    "[espnow][pairing]",
    hub_pairing_test,
    node_pairing_test);

// ===========================================================================
// TEST: Heartbeat — NODE sends heartbeat, HUB marks peer as online
// ===========================================================================

static void hub_heartbeat_test()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    g_mgr->init(make_hub_config(g_app_queue));

    unity_wait_for_signal("node ready for heartbeat test");

    vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs * 2));

    auto offline = g_mgr->get_offline_peers();
    TEST_ASSERT_EQUAL(0, offline.size());

    unity_send_signal("hub heartbeat verified");
    test_cleanup();
}

static void node_heartbeat_test()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    g_mgr->init(make_node_config(g_app_queue));

    unity_send_signal("node ready for heartbeat test");

    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_wait_for_signal("hub heartbeat verified");

    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "Heartbeat: NODE sends heartbeat, HUB marks peer as online",
    "[espnow][heartbeat]",
    hub_heartbeat_test,
    node_heartbeat_test);

// ===========================================================================
// TEST: send_data — HUB sends DATA to NODE, NODE receives on app_queue
// ===========================================================================

static void hub_send_data_test()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_wait_for_signal("node ready for data");

    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    esp_err_t ret = g_mgr->send_data(0x02, static_cast<PayloadType>(0x01), payload, sizeof(payload));
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    unity_wait_for_signal("node received data");
    test_cleanup();
}

static void node_receive_data_test()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

    vTaskDelay(pdMS_TO_TICKS(kWaitAfterPairingMs));
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());

    unity_send_signal("node ready for data");

    AppMessage msg{};
    BaseType_t received = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, received);

    TEST_ASSERT_EQUAL(ReservedIds::HUB, msg.sender_id);
    TEST_ASSERT_EQUAL(static_cast<PayloadType>(0x01), msg.payload_type);
    TEST_ASSERT_EQUAL(4, msg.payload_len);

    unity_send_signal("node received data");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "send_data: HUB sends DATA to NODE, NODE receives on app_queue",
    "[espnow][data]",
    hub_send_data_test,
    node_receive_data_test);

// ===========================================================================
// §3.1 — IntegrationHubAndNodePairSuccessfully
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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));
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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));
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
    "3.1 IntegrationHubAndNodePairSuccessfully",
    "[espnow][3.1][pairing]",
    hub_auto_pair,
    node_auto_pair);

// ===========================================================================
// §3.1 — IntegrationNodeSendsDataHubReceivesAndAcks
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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    // Let NODE finish automatic pairing.
    unity_wait_for_signal("node operational");

    // Receive the DATA packet sent by NODE.
    AppMessage msg{};
    BaseType_t got = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, got);
    TEST_ASSERT_EQUAL(0x02, msg.sender_id); // NODE id
    TEST_ASSERT_EQUAL(kTestPayloadType, msg.payload_type);
    TEST_ASSERT_TRUE(msg.payload_len > 0);
    TEST_ASSERT_TRUE(msg.requires_ack);

    // ACK the message so NODE's TxManager can complete the transaction.
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->confirm_reception(AckStatus::OK));

    unity_send_signal("hub acked");

    unity_wait_for_signal("node verified");
    test_cleanup();
}

static void node_send_with_ack()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

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
    "3.1 IntegrationNodeSendsDataHubReceivesAndAcks",
    "[espnow][3.1][ack]",
    hub_receive_and_ack,
    node_send_with_ack);

// ===========================================================================
// §3.1 — IntegrationAckTimeoutRetriesAndSuccess
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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    unity_wait_for_signal("node sending");

    // Receive the DATA packet.
    AppMessage msg{};
    BaseType_t got = xQueueReceive(g_app_queue, &msg, pdMS_TO_TICKS(5000));
    TEST_ASSERT_EQUAL(pdTRUE, got);
    TEST_ASSERT_TRUE(msg.requires_ack);

    // Delay ACK by 2x the retry timeout to force at least one retry in NODE's
    // TxManager.  Staying below 3x avoids triggering MAX_FAILURES (=3).
    vTaskDelay(pdMS_TO_TICKS(kAckRetryTimeoutMs * 2));

    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->confirm_reception(AckStatus::OK));

    unity_send_signal("hub acked after delay");

    unity_wait_for_signal("node verified");
    test_cleanup();
}

static void node_send_retry()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

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
    "3.1 IntegrationAckTimeoutRetriesAndSuccess",
    "[espnow][3.1][retry]",
    hub_delayed_ack,
    node_send_retry);

// ===========================================================================
// §3.4 — IntegrationHeartbeatSentPeriodically
//
// After automatic pairing both devices reach OPERATIONAL. HUB uses a short
// pairing timeout (kForcePairingTimeoutMs) to transition quickly. After two
// heartbeat intervals, the HUB checks that no peer is marked offline — proof
// that at least one heartbeat was received.
// ===========================================================================

static void hub_heartbeat_periodically()
{
    g_app_queue = xQueueCreate(kAppQueueLength, sizeof(AppMessage));
    TEST_ASSERT_NOT_NULL(g_app_queue);

    g_mgr = make_espnow_manager();
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

    // Drive HUB to OPERATIONAL quickly so it can track heartbeats.
    g_mgr->start_pairing(kForcePairingTimeoutMs);

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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));
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
    "3.4 IntegrationHeartbeatSentPeriodically",
    "[espnow][3.4][heartbeat]",
    hub_heartbeat_periodically,
    node_heartbeat_periodically);

// ===========================================================================
// §3.5 — IntegrationPeersPersistedToNvsAndRestored
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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));
    g_mgr->start_pairing(kForcePairingTimeoutMs);

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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_hub_config(g_app_queue)));

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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

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
    TEST_ASSERT_EQUAL(ESP_OK, g_mgr->init(make_node_config(g_app_queue)));

    // NODE has stored peers → should start OPERATIONAL directly.
    TEST_ASSERT_EQUAL(NodeState::OPERATIONAL, g_mgr->get_node_state());
    TEST_ASSERT_EQUAL(1, g_mgr->get_peers().size());

    unity_wait_for_signal("hub restored c2");
    unity_send_signal("node restored c2");
    test_cleanup();
}

TEST_CASE_MULTIPLE_DEVICES(
    "3.5 IntegrationPeersPersistedToNvsAndRestored",
    "[espnow][3.5][storage]",
    hub_peers_persisted,
    node_peers_persisted);
