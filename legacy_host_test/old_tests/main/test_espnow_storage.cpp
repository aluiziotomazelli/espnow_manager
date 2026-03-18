// test_espnow_storage.cpp
#include "esp_log.h"
#include "espnow_storage.hpp"
#include "nvs_flash.h"
#include "protocol_types.hpp"
#include "test_memory_helper.h"
#include "unity.h"

enum class TestNodeId : NodeId
{
    HUB          = 1,
    WATER_TANK   = 5,
    SOLAR_SENSOR = 7,
    PUMP_CONTROL = 10,
    WEATHER      = 12,
};

enum class TestNodeType : NodeType
{
    HUB    = 1,
    SENSOR = 2
};

// ============================================================================
// Group 1: Basic Functionality Tests
// ============================================================================

/**
 * Test 1.1: Basic Load/Save Operation
 * Save a simple data set and load it back, verifying integrity.
 */
TEST_CASE("storage_basic_load_save", "[espnow][storage][basic][loadsave]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;
        uint8_t channel = 6;
        std::vector<PersistentPeer> peers;

        // Create test peer
        PersistentPeer test_peer;
        test_peer.mac[0]                = 0x11;
        test_peer.mac[1]                = 0x22;
        test_peer.mac[2]                = 0x33;
        test_peer.mac[3]                = 0x44;
        test_peer.mac[4]                = 0x55;
        test_peer.mac[5]                = 0x66;
        test_peer.type                  = to_node_type(TestNodeType::SENSOR);
        test_peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
        test_peer.channel               = 6;
        test_peer.paired                = true;
        test_peer.heartbeat_interval_ms = 5000;

        peers.push_back(test_peer);

        // Test save
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

        // Test load
        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        // Verify
        TEST_ASSERT_EQUAL(channel, loaded_channel);
        TEST_ASSERT_EQUAL(1, loaded_peers.size());
        TEST_ASSERT_EQUAL_HEX8_ARRAY(test_peer.mac, loaded_peers[0].mac, 6);

        // Clean up for next test
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

/**
 * Test 1.2: Multiple Peers Operation
 * Save and load multiple peers, verifying integrity.
 */
TEST_CASE("storage_multiple_peers", "[espnow][storage][basic][peers]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;
        uint8_t channel = 6;
        std::vector<PersistentPeer> peers;

        // Array of valid NodeIds for testing
        NodeId valid_node_ids[] = {to_node_id(TestNodeId::HUB),
                                   to_node_id(TestNodeId::WATER_TANK),
                                   to_node_id(TestNodeId::SOLAR_SENSOR),
                                   to_node_id(TestNodeId::PUMP_CONTROL),
                                   to_node_id(TestNodeId::WEATHER)};

        // Add multiple peers (using valid NodeIds)
        for (int i = 0; i < 5; i++) {
            PersistentPeer peer;
            peer.mac[0]                = 0x11;
            peer.mac[1]                = 0x22;
            peer.mac[2]                = 0x33;
            peer.mac[3]                = 0x44;
            peer.mac[4]                = 0x55;
            peer.mac[5]                = static_cast<uint8_t>(0x66 + i);
            peer.type                  = (i == 0) ? to_node_type(TestNodeType::HUB) : to_node_type(TestNodeType::SENSOR);
            peer.node_id               = valid_node_ids[i];
            peer.channel               = static_cast<uint8_t>(6 + i);
            peer.paired                = true;
            peer.heartbeat_interval_ms = static_cast<uint32_t>(5000 + i * 1000);

            peers.push_back(peer);
        }

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        TEST_ASSERT_EQUAL(channel, loaded_channel);
        TEST_ASSERT_EQUAL(5, loaded_peers.size());

        // Clean storage
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

/**
 * Test 1.3: Peer Field Persistence
 * Ensure all Peer fields are correctly persisted and restored.
 */
TEST_CASE("storage_peer_field_persistence", "[espnow][storage][basic][fields]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // Test persistence of all Peer fields
        PersistentPeer peer;
        uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        memcpy(peer.mac, mac, 6);
        peer.type                  = to_node_type(TestNodeType::HUB);
        peer.node_id               = to_node_id(TestNodeId::WATER_TANK);
        peer.channel               = 11;
        peer.paired                = false;
        peer.heartbeat_interval_ms = 12345;

        std::vector<PersistentPeer> peers = {peer};
        uint8_t channel                        = 8;

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        TEST_ASSERT_EQUAL(1, loaded_peers.size());
        TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, loaded_peers[0].mac, 6);
        TEST_ASSERT_EQUAL(static_cast<int>(TestNodeType::HUB), static_cast<int>(loaded_peers[0].type));
        TEST_ASSERT_EQUAL(static_cast<int>(TestNodeId::WATER_TANK), static_cast<int>(loaded_peers[0].node_id));
        TEST_ASSERT_EQUAL(11, loaded_peers[0].channel);
        TEST_ASSERT_EQUAL(false, loaded_peers[0].paired);
        TEST_ASSERT_EQUAL(12345, loaded_peers[0].heartbeat_interval_ms);

        // Clean up
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

// ============================================================================
// Group 2: CRC and Data Validation Tests
// ============================================================================

/**
 * Test 2.1: CRC Validation
 * Corrupt saved data and ensure load fails due to CRC verification.
 */
TEST_CASE("storage_crc_validation", "[espnow][storage][validation][crc]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // 1. Reset RTC BEFORE starting
        EspNowStorage::test_reset_rtc();

        // 2. Save valid data (will go to NVS and RTC)
        uint8_t channel = 6;
        std::vector<PersistentPeer> valid_peers;

        PersistentPeer valid_peer;
        memset(valid_peer.mac, 0xAA, 6);
        valid_peer.type                  = to_node_type(TestNodeType::SENSOR);
        valid_peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
        valid_peer.channel               = 6;
        valid_peer.paired                = true;
        valid_peer.heartbeat_interval_ms = 5000;
        valid_peers.push_back(valid_peer);

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, valid_peers, true));

        // 3. NOW corrupt the CRC in NVS
        nvs_handle_t handle;
        TEST_ASSERT_EQUAL(ESP_OK, nvs_open("espnow_store", NVS_READWRITE, &handle));

        // Read current data from NVS
        EspNowStorage::PersistentData nvs_data;
        size_t size   = sizeof(EspNowStorage::PersistentData);
        esp_err_t err = nvs_get_blob(handle, "persist_data", &nvs_data, &size);

        TEST_ASSERT_EQUAL(ESP_OK, err);
        TEST_ASSERT_EQUAL(sizeof(EspNowStorage::PersistentData), size);

        // Corrupt the CRC
        nvs_data.crc = 0xDEADBEEF; // Invalid CRC

        // Save corrupted data back to NVS
        TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(handle, "persist_data", &nvs_data, size));
        TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
        nvs_close(handle);

        // 4. Load should use RTC (which has valid data) and work
        // First verify that RTC has valid data
        auto &rtc_data = EspNowStorage::test_get_rtc();
        TEST_ASSERT_EQUAL(EspNowStorage::ESPNOW_STORAGE_MAGIC, rtc_data.magic);

        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        // 5. NOW reset RTC to force use of corrupted NVS
        EspNowStorage::test_reset_rtc();

        // 6. Try to load with invalid RTC and corrupted NVS - should fail
        TEST_ASSERT_EQUAL(ESP_FAIL, storage.load(loaded_channel, loaded_peers));

        // Clean up
        EspNowStorage::test_reset_rtc();
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

/**
 * Test 2.2: Version and Magic Validation
 * Save data with incorrect magic or version and ensure load fails.
 */
TEST_CASE("storage_version_magic_check", "[espnow][storage][validation][magic][version]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // 1. First ensure the namespace exists by saving valid data
        uint8_t channel = 6;
        std::vector<PersistentPeer> valid_peers;
        storage.save(channel, valid_peers, true);

        // 2. NOW reset RTC
        EspNowStorage::test_reset_rtc();

        // 3. Corrupt NVS with wrong magic
        nvs_handle_t handle;
        esp_err_t err = nvs_open("espnow_store", NVS_READWRITE, &handle);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "nvs_open should succeed");

        // Create data with wrong magic
        EspNowStorage::PersistentData wrong_magic;
        memset(&wrong_magic, 0, sizeof(wrong_magic));
        wrong_magic.magic        = 0xDEADBEEF; // Wrong magic
        wrong_magic.version      = EspNowStorage::ESPNOW_STORAGE_VERSION;
        wrong_magic.wifi_channel = 6;
        wrong_magic.num_peers    = 0;
        // Calculate correct CRC for THIS structure
        wrong_magic.crc = EspNowStorage::test_calculate_crc(wrong_magic);

        err = nvs_set_blob(handle, "persist_data", &wrong_magic, sizeof(wrong_magic));
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = nvs_commit(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        nvs_close(handle);

        // 4. Try to load - should fail because magic is wrong
        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        err = storage.load(loaded_channel, loaded_peers);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, err, "Should fail with wrong magic");

        // 5. Test wrong version
        EspNowStorage::test_reset_rtc(); // Reset RTC again

        err = nvs_open("espnow_store", NVS_READWRITE, &handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        EspNowStorage::PersistentData wrong_version;
        memset(&wrong_version, 0, sizeof(wrong_version));
        wrong_version.magic        = EspNowStorage::ESPNOW_STORAGE_MAGIC;
        wrong_version.version      = 999; // Wrong version
        wrong_version.wifi_channel = 6;
        wrong_version.num_peers    = 0;
        wrong_version.crc          = EspNowStorage::test_calculate_crc(wrong_version);

        TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(handle, "persist_data", &wrong_version, sizeof(wrong_version)));
        TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
        nvs_close(handle);

        // 6. Try to load - should fail because version is wrong
        err = storage.load(loaded_channel, loaded_peers);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, err, "Should fail with wrong version");

        // 7. Clean up
        EspNowStorage::test_reset_rtc();
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

// ============================================================================
// Group 3: Edge Cases and Limits Tests
// ============================================================================

/**
 * Test 3.1: Maximum Peer Limit
 * Try to save more peers than the maximum limit and ensure only the allowed
 * number is saved.
 */
TEST_CASE("storage_max_peers_limit", "[espnow][storage][limits][peers]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;
        uint8_t channel = 6;
        std::vector<PersistentPeer> peers;

        // Create more peers than the limit
        for (int i = 0; i < EspNowStorage::MAX_PERSISTENT_PEERS + 5; i++) {
            PersistentPeer peer;
            memset(peer.mac, i, 6);
            peer.type                  = to_node_type(TestNodeType::SENSOR);
            peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
            peer.channel               = 6;
            peer.paired                = true;
            peer.heartbeat_interval_ms = 5000;
            peers.push_back(peer);
        }

        // Save should truncate to MAX_PERSISTENT_PEERS
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        // Verify that only MAX_PERSISTENT_PEERS were saved
        TEST_ASSERT_EQUAL(EspNowStorage::MAX_PERSISTENT_PEERS, loaded_peers.size());

        // Clean up
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

/**
 * Test 3.2: Edge Cases for Number of Peers
 * Try to save more peers than the limit.maxcdn and ensure only the
 * allowed number is saved.
 */
TEST_CASE("storage_edge_cases_num_peers", "[espnow][storage][limits][edgecases]")

{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // Test with 0 peers (edge case)
        uint8_t channel = 6;
        std::vector<PersistentPeer> empty_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, empty_peers, true));

        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));
        TEST_ASSERT_EQUAL(0, loaded_peers.size());

        // Test with exactly MAX_PERSISTENT_PEERS
        std::vector<PersistentPeer> max_peers;
        for (int i = 0; i < EspNowStorage::MAX_PERSISTENT_PEERS; i++) {
            PersistentPeer peer;
            memset(peer.mac, i, 6);
            peer.type                  = to_node_type(TestNodeType::SENSOR);
            peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
            peer.channel               = 6;
            peer.paired                = true;
            peer.heartbeat_interval_ms = 5000;
            max_peers.push_back(peer);
        }

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, max_peers, true));
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));
        TEST_ASSERT_EQUAL(EspNowStorage::MAX_PERSISTENT_PEERS, loaded_peers.size());

        // Clean up
        storage.save(1, empty_peers, true);
    }
}

// ============================================================================
// Group 4: NVS Corruption and Recovery Tests
// ============================================================================

/**
 * Test 4.1: Corrupted and Incomplete Data
 * Simulate scenarios where data in NVS is corrupted or incomplete and
 * ensure load fails gracefully.
 */
TEST_CASE("storage_nvs_corrupted_empty", "[espnow][storage][recovery][corruption]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // 1. FIRST save valid data to ensure the namespace exists
        uint8_t channel = 6;
        std::vector<PersistentPeer> valid_peers;

        PersistentPeer peer;
        memset(peer.mac, 0xAA, 6);
        peer.type                  = to_node_type(TestNodeType::SENSOR);
        peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
        peer.channel               = 6;
        peer.paired                = true;
        peer.heartbeat_interval_ms = 5000;
        valid_peers.push_back(peer);

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, valid_peers, true));

        // 2. NOW reset RTC to force use of NVS
        EspNowStorage::test_reset_rtc();

        // 3. Delete key from NVS
        nvs_handle_t handle;
        esp_err_t err = nvs_open("espnow_store", NVS_READWRITE, &handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = nvs_erase_key(handle, "persist_data");
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = nvs_commit(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        nvs_close(handle);

        // 4. Try to load with empty RTC and no key in NVS
        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        err = storage.load(loaded_channel, loaded_peers);

        TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_NVS_NOT_FOUND, err,
                                  "Should return ESP_ERR_NVS_NOT_FOUND when key doesn't exist");

        // 5. Test with wrong size data
        // First ensure empty RTC
        EspNowStorage::test_reset_rtc();

        err = nvs_open("espnow_store", NVS_READWRITE, &handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        uint8_t small_data[10] = {0};
        err                    = nvs_set_blob(handle, "persist_data", small_data, sizeof(small_data));
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = nvs_commit(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        nvs_close(handle);

        // 6. Try to load with wrong size data
        err = storage.load(loaded_channel, loaded_peers);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_SIZE, err, "Should fail with wrong blob size");

        // 7. Complete cleanup
        // First reset RTC
        EspNowStorage::test_reset_rtc();

        // Then clear NVS
        err = nvs_open("espnow_store", NVS_READWRITE, &handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = nvs_erase_key(handle, "persist_data");
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = nvs_commit(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        nvs_close(handle);

        // Finally save valid empty data
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

/**
 * Test 4.2: Corrupted Data Recovery
 * Simulate data corruption in NVS and ensure the system can recover
 * by saving new data.
 */
TEST_CASE("storage_nvs_recovery_scenario", "[espnow][storage][recovery][scenario]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // 1. Save valid data
        uint8_t channel = 6;
        std::vector<PersistentPeer> peers;

        PersistentPeer peer;
        memset(peer.mac, 0xAA, 6);
        peer.type                  = to_node_type(TestNodeType::SENSOR);
        peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
        peer.channel               = 6;
        peer.paired                = true;
        peer.heartbeat_interval_ms = 5000;
        peers.push_back(peer);

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

        // 2. Simulate NVS corruption (correct size, garbage content)
        nvs_handle_t handle;
        TEST_ASSERT_EQUAL(ESP_OK, nvs_open("espnow_store", NVS_READWRITE, &handle));

        EspNowStorage::PersistentData garbage;
        memset(&garbage, 0xFF, sizeof(garbage)); // Fill with 0xFF
        garbage.crc = 0xFFFFFFFF;                // Invalid CRC

        TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(handle, "persist_data", &garbage, sizeof(garbage)));
        TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
        nvs_close(handle);

        // 3. Reset RTC to force use of corrupted NVS
        EspNowStorage::test_reset_rtc();

        // 4. Load should fail
        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_NOT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        // 5. New save should overwrite corrupted data
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

        // 6. Now load should work
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        // Clean up
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

// ============================================================================
// Group 5: Performance and Optimization Tests
// ============================================================================

/**
 * Test 5.1: Optimized Save Operation
 * Verify if optimized save (force_nvs_commit = false) works correctly.
 */
TEST_CASE("storage_optimized_save_no_changes", "[espnow][storage][performance][optimization]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;
        uint8_t channel = 6;
        std::vector<PersistentPeer> peers;

        // Add a peer
        PersistentPeer peer;
        memset(peer.mac, 0xAA, 6);
        peer.type                  = to_node_type(TestNodeType::HUB);
        peer.node_id               = to_node_id(TestNodeId::HUB);
        peer.channel               = 6;
        peer.paired                = true;
        peer.heartbeat_interval_ms = 10000;
        peers.push_back(peer);

        // First save (should go to NVS)
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

        // Second save with same data and force_nvs_commit = false
        // Should not write to NVS (optimization)
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, false));

        // Change data and save with force_nvs_commit = false
        // Should save because there is a real change
        std::vector<PersistentPeer> new_peers;
        PersistentPeer new_peer;
        memset(new_peer.mac, 0xBB, 6);
        new_peer.type                  = to_node_type(TestNodeType::SENSOR);
        new_peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
        new_peer.channel               = 11;
        new_peer.paired                = false;
        new_peer.heartbeat_interval_ms = 5000;
        new_peers.push_back(new_peer);

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(11, new_peers, false));

        // Clean up
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

/**
 * Test 5.2: Memory/Performance Check
 * Verify there are no memory leaks or excessive usage after multiple
 * save/load operations.
 */
TEST_CASE("storage_memory_leak_check", "[espnow][storage][performance][memory]")
{
    // Use more restrictive limits to detect leaks
    set_memory_leak_threshold(-1000);

    {
        EspNowStorage storage;

        // Execute multiple operations to detect accumulation
        for (int i = 0; i < 10; i++) {
            uint8_t channel = 6;
            std::vector<PersistentPeer> peers;

            PersistentPeer peer;
            memset(peer.mac, i, 6);
            peer.type                  = to_node_type(TestNodeType::SENSOR);
            peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
            peer.channel               = 6;
            peer.paired                = true;
            peer.heartbeat_interval_ms = 5000;
            peers.push_back(peer);

            TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

            uint8_t loaded_channel;
            std::vector<PersistentPeer> loaded_peers;
            TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));
        }

        // Clean up
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

// ============================================================================
// Group 6: RTC vs NVS Priority Tests
// ============================================================================

/**
 * Test 6.1: RTC vs NVS Priority
 * Ensure that data in RTC has priority over data in NVS during load.
 */
TEST_CASE("storage_priority_rtc_over_nvs", "[espnow][storage][priority][rtc]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // 1. Save data "A" in RTC (via normal save)
        uint8_t channel_a = 11;
        std::vector<PersistentPeer> peers_a;

        PersistentPeer peer_a;
        memset(peer_a.mac, 0xAA, 6);
        peer_a.type                  = to_node_type(TestNodeType::HUB);
        peer_a.node_id               = to_node_id(TestNodeId::HUB);
        peer_a.channel               = 11;
        peer_a.paired                = true;
        peer_a.heartbeat_interval_ms = 10000;
        peers_a.push_back(peer_a);

        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel_a, peers_a, true));
        // Now rtc_storage contains data "A"

        // 2. Manipulate NVS directly to have different data "B"
        nvs_handle_t handle;
        TEST_ASSERT_EQUAL(ESP_OK, nvs_open("espnow_store", NVS_READWRITE, &handle));

        // Create different data "B"
        uint8_t channel_b = 6;
        std::vector<PersistentPeer> peers_b;

        PersistentPeer peer_b;
        memset(peer_b.mac, 0xBB, 6);
        peer_b.type                  = to_node_type(TestNodeType::SENSOR);
        peer_b.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
        peer_b.channel               = 6;
        peer_b.paired                = true;
        peer_b.heartbeat_interval_ms = 5000;
        peers_b.push_back(peer_b);

        // Save data "B" in NVS
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel_b, peers_b, true));
        // Now both RTC and NVS have data "B"

        // 3. Modify only RTC to have data "A" again
        // We need to do this indirectly:
        // - Save data "A" again
        // - But NVS continues with data "B"
        TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel_a, peers_a, false));
        // force_nvs_commit = false, so NVS is not updated

        // 4. Load should prioritize RTC (data "A")
        uint8_t loaded_channel;
        std::vector<PersistentPeer> loaded_peers;
        TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

        // Should load from RTC (channel 11, mac 0xAA)
        TEST_ASSERT_EQUAL(11, loaded_channel);
        TEST_ASSERT_EQUAL(1, loaded_peers.size());
        TEST_ASSERT_EQUAL_HEX8(0xAA, loaded_peers[0].mac[0]);

        // Clean up
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

// ============================================================================
// Group 7: Concurrency and Stress Tests
// ============================================================================

/**
 * Test 7.1: Simulated Concurrency
 * Simulate multiple threads trying to save and load data simultaneously
 * to ensure EspNowStorage handles race conditions correctly.
 */
TEST_CASE("storage_multiple_save_load_sequence", "[espnow][storage][concurrency][sequence]")
{
    set_memory_leak_threshold(-2000);
    {
        EspNowStorage storage;

        // Sequence of save/load to simulate real usage
        for (int i = 1; i <= 3; i++) {
            uint8_t channel = static_cast<uint8_t>(i + 5);
            std::vector<PersistentPeer> peers;

            // Add i peers
            for (int j = 0; j < i; j++) {
                PersistentPeer peer;
                memset(peer.mac, static_cast<uint8_t>(0x10 * i + j), 6);
                peer.type                  = (j == 0) ? to_node_type(TestNodeType::HUB) : to_node_type(TestNodeType::SENSOR);
                peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
                peer.channel               = channel;
                peer.paired                = (j % 2 == 0);
                peer.heartbeat_interval_ms = static_cast<uint32_t>(5000 + j * 1000);
                peers.push_back(peer);
            }

            TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

            uint8_t loaded_channel;
            std::vector<PersistentPeer> loaded_peers;
            TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

            TEST_ASSERT_EQUAL(channel, loaded_channel);
            TEST_ASSERT_EQUAL(i, loaded_peers.size());
        }

        // Clean up at the end
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}

/**
 * Test 7.2: Stress Test
 * Perform multiple save/load operations in rapid sequence to
 * ensure stability under load.
 */
TEST_CASE("storage_stress_test", "[espnow][storage][stress][performance]")
{
    set_memory_leak_threshold(-4000); // Larger limits for stress test

    {
        EspNowStorage storage;

        // Execute many operations in rapid sequence
        const int iterations = 50;
        for (int i = 0; i < iterations; i++) {
            uint8_t channel = static_cast<uint8_t>(i % 14 + 1); // Channels 1-14
            std::vector<PersistentPeer> peers;

            // Variable number of peers
            int num_peers = i % (EspNowStorage::MAX_PERSISTENT_PEERS + 1);
            for (int j = 0; j < num_peers; j++) {
                PersistentPeer peer;
                memset(peer.mac, static_cast<uint8_t>(i + j), 6);
                peer.type                  = (j % 2 == 0) ? to_node_type(TestNodeType::HUB) : to_node_type(TestNodeType::SENSOR);
                peer.node_id               = to_node_id(TestNodeId::SOLAR_SENSOR);
                peer.channel               = static_cast<uint8_t>(j % 14 + 1);
                peer.paired                = (j % 3 == 0);
                peer.heartbeat_interval_ms = static_cast<uint32_t>(1000 + j * 100);
                peers.push_back(peer);
            }

            TEST_ASSERT_EQUAL(ESP_OK, storage.save(channel, peers, true));

            uint8_t loaded_channel;
            std::vector<PersistentPeer> loaded_peers;
            TEST_ASSERT_EQUAL(ESP_OK, storage.load(loaded_channel, loaded_peers));

            // Basic verification
            TEST_ASSERT_EQUAL(channel, loaded_channel);
            TEST_ASSERT_EQUAL(num_peers, static_cast<int>(loaded_peers.size()));
        }

        // Clean up
        std::vector<PersistentPeer> empty_peers;
        storage.save(1, empty_peers, true);
    }
}
