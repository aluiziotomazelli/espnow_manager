#include "unity.h"
#include "espnow_storage.hpp"
#include "espnow_storage_backends.hpp"
#include "protocol_types.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstring>
#include <vector>
#include <memory>
#include <algorithm>

/**
 * @file test_espnow_storage.cpp
 * @brief Unit tests for the EspNowStorage class and its backends.
 *
 * The storage system uses a two-tier approach:
 * 1. RTC Memory: Fast, surviving deep sleep, but lost on power cycle.
 * 2. NVS (Flash): Permanent storage, slower, limited write cycles.
 *
 * Logic includes CRC validation, version checking, and a "dirty check" to minimize Flash writes.
 */

// --- Mock Persistence Backend ---

/**
 * @brief A simple implementation of IPersistenceBackend that uses an in-memory buffer.
 * Used to test the high-level logic of EspNowStorage in isolation.
 */
class MockPersistenceBackend : public IPersistenceBackend {
public:
    std::vector<uint8_t> buffer;
    esp_err_t load_ret = ESP_OK;
    esp_err_t save_ret = ESP_OK;
    int load_calls = 0;
    int save_calls = 0;

    esp_err_t load(void *data, size_t size) override {
        load_calls++;
        if (load_ret != ESP_OK) return load_ret;
        if (buffer.size() < size) return ESP_ERR_NOT_FOUND;
        memcpy(data, buffer.data(), size);
        return ESP_OK;
    }

    esp_err_t save(const void *data, size_t size) override {
        save_calls++;
        if (save_ret != ESP_OK) return save_ret;
        // Store the saved data in our internal buffer
        buffer.assign((const uint8_t*)data, (const uint8_t*)data + size);
        return ESP_OK;
    }

    void reset() {
        buffer.clear();
        load_ret = ESP_OK;
        save_ret = ESP_OK;
        load_calls = 0;
        save_calls = 0;
    }
};

// --- Test Fixtures ---

static MockPersistenceBackend* mock_rtc;
static MockPersistenceBackend* mock_nvs;
static EspNowStorage* storage;

void setUp(void) {
    // Create new mocks for each test to ensure isolation.
    mock_rtc = new MockPersistenceBackend();
    mock_nvs = new MockPersistenceBackend();
    // Inject the mocks into the EspNowStorage instance.
    storage = new EspNowStorage(
        std::unique_ptr<IPersistenceBackend>(mock_rtc),
        std::unique_ptr<IPersistenceBackend>(mock_nvs)
    );
}

void tearDown(void) {
    delete storage;
    // Mock backends are automatically deleted by the storage's unique_ptr.
}

/**
 * @brief Helper to generate a list of dummy peers for testing.
 */
static std::vector<PersistentPeer> create_test_peers(int count) {
    std::vector<PersistentPeer> peers;
    for (int i = 0; i < count; ++i) {
        PersistentPeer p;
        memset(&p, 0, sizeof(p));
        p.node_id = (uint8_t)(i + 10);
        p.channel = 1;
        p.type = 2; // SENSOR
        memset(p.mac, i, 6);
        peers.push_back(p);
    }
    return peers;
}

// --- Test Cases ---

TEST_CASE("EspNowStorage Save and Load (Happy Path)", "[storage]")
{
    uint8_t wifi_channel = 6;
    auto peers = create_test_peers(5);

    // Scenario: Saving valid data should write to both RTC and NVS.
    TEST_ASSERT_EQUAL(ESP_OK, storage->save(wifi_channel, peers, true));

    // Verify both backends were called.
    TEST_ASSERT_EQUAL(1, mock_rtc->save_calls);
    TEST_ASSERT_EQUAL(1, mock_nvs->save_calls);

    // Scenario: Loading should retrieve the exact same data.
    uint8_t loaded_channel = 0;
    std::vector<PersistentPeer> loaded_peers;
    TEST_ASSERT_EQUAL(ESP_OK, storage->load(loaded_channel, loaded_peers));

    // Verify integrity of loaded data.
    TEST_ASSERT_EQUAL(wifi_channel, loaded_channel);
    TEST_ASSERT_EQUAL(peers.size(), loaded_peers.size());
    for (size_t i = 0; i < peers.size(); ++i) {
        TEST_ASSERT_EQUAL(peers[i].node_id, loaded_peers[i].node_id);
        TEST_ASSERT_EQUAL_MEMORY(peers[i].mac, loaded_peers[i].mac, 6);
    }
}

TEST_CASE("EspNowStorage Peer Limit Truncation", "[storage]")
{
    // Scenario: Try to save 20 peers, but the system limit is 19.
    auto many_peers = create_test_peers(20);

    // The storage should accept the call but truncate the internal list.
    TEST_ASSERT_EQUAL(ESP_OK, storage->save(1, many_peers, true));

    uint8_t ch;
    std::vector<PersistentPeer> loaded;
    TEST_ASSERT_EQUAL(ESP_OK, storage->load(ch, loaded));

    // Verify it was truncated to exactly 19 without crashing.
    TEST_ASSERT_EQUAL(19, loaded.size());
    TEST_ASSERT_EQUAL(many_peers[18].node_id, loaded[18].node_id);
}

TEST_CASE("EspNowStorage detects CRC corruption", "[storage]")
{
    // First, save valid data.
    storage->save(1, create_test_peers(1), true);

    // Simulate hardware corruption by flipping bits in both backend buffers.
    mock_rtc->buffer[10] ^= 0xFF;
    mock_nvs->buffer[10] ^= 0xFF;

    uint8_t ch;
    std::vector<PersistentPeer> loaded;
    // The load operation should detect CRC mismatch and return NOT_FOUND instead of garbage.
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, storage->load(ch, loaded));
}

TEST_CASE("EspNowStorage Priority: RTC Preferred over NVS", "[storage]")
{
    // Scenario: Valid but different data exists in both RTC and NVS.
    uint8_t rtc_channel = 1;
    auto rtc_peers = create_test_peers(1);
    storage->save(rtc_channel, rtc_peers, true);

    uint8_t nvs_channel = 13;
    auto nvs_peers = create_test_peers(2);
    // Manually inject different valid data into the NVS buffer.
    {
        auto tmp_rtc_ptr = std::make_unique<MockPersistenceBackend>();
        auto tmp_nvs_ptr = std::make_unique<MockPersistenceBackend>();
        MockPersistenceBackend* tmp_nvs_ref = tmp_nvs_ptr.get();
        EspNowStorage tmp_storage(std::move(tmp_rtc_ptr), std::move(tmp_nvs_ptr));
        tmp_storage.save(nvs_channel, nvs_peers, true);
        mock_nvs->buffer = tmp_nvs_ref->buffer;
    }

    uint8_t loaded_ch;
    std::vector<PersistentPeer> loaded_peers;
    TEST_ASSERT_EQUAL(ESP_OK, storage->load(loaded_ch, loaded_peers));

    // High-level logic: RTC should be checked first and preferred for speed.
    TEST_ASSERT_EQUAL(rtc_channel, loaded_ch);
    TEST_ASSERT_EQUAL(rtc_peers.size(), loaded_peers.size());
}

TEST_CASE("EspNowStorage Priority: Fallback to NVS if RTC invalid", "[storage]")
{
    // Scenario: RTC is corrupted but NVS contains a valid backup.
    uint8_t nvs_channel = 13;
    auto nvs_peers = create_test_peers(2);
    storage->save(nvs_channel, nvs_peers, true);

    // Corrupt only the RTC data.
    mock_rtc->buffer[0] ^= 0xFF;

    uint8_t loaded_ch;
    std::vector<PersistentPeer> loaded_peers;
    TEST_ASSERT_EQUAL(ESP_OK, storage->load(loaded_ch, loaded_peers));

    // The system should detect RTC failure and fallback to NVS.
    TEST_ASSERT_EQUAL(nvs_channel, loaded_ch);

    // Fallback logic should also synchronize the valid data back to RTC for future loads.
    mock_nvs->reset(); // Clear NVS to prove next load comes from RTC.
    TEST_ASSERT_EQUAL(ESP_OK, storage->load(loaded_ch, loaded_peers));
    TEST_ASSERT_EQUAL(nvs_channel, loaded_ch);
}

TEST_CASE("EspNowStorage Smart Save (Dirty Check)", "[storage]")
{
    uint8_t ch = 1;
    auto peers = create_test_peers(1);

    // Initial save: writes to both backends.
    storage->save(ch, peers, true);
    int rtc_saves = mock_rtc->save_calls;
    int nvs_saves = mock_nvs->save_calls;

    // Save identical data with force_nvs_commit = false.
    storage->save(ch, peers, false);

    // Verify optimization: NVS write (expensive) should be skipped if data hasn't changed.
    TEST_ASSERT_EQUAL(rtc_saves, mock_rtc->save_calls);
    TEST_ASSERT_EQUAL(nvs_saves, mock_nvs->save_calls);

    // Save identical data but explicitly force NVS commit.
    storage->save(ch, peers, true);

    // Verify NVS was written this time.
    TEST_ASSERT_EQUAL(nvs_saves + 1, mock_nvs->save_calls);
}

TEST_CASE("EspNowStorage handles version mismatch", "[storage]")
{
    // Scenario: Code update changes the persistent data structure version.
    storage->save(1, create_test_peers(1), true);

    // Manually simulate an old version in stored data.
    mock_rtc->buffer[4] = 99; // Assume version 1 in code, 99 in memory
    mock_nvs->buffer[4] = 99;

    uint8_t ch;
    std::vector<PersistentPeer> loaded;
    // The system should invalidate/ignore the incompatible old data.
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, storage->load(ch, loaded));
}

TEST_CASE("EspNowStorage propagates NVS errors", "[storage]")
{
    // Scenario: Flash memory is full or failing.
    mock_nvs->save_ret = ESP_ERR_NVS_NOT_FOUND;

    // The storage class should propagate the error to the caller.
    esp_err_t err = storage->save(1, create_test_peers(1), true);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);
}

TEST_CASE("RealRtcBackend logic", "[storage_backend]")
{
    // Test the real RTC backend using a local memory buffer instead of the static global.
    PersistentData my_storage;
    memset(&my_storage, 0, sizeof(my_storage));
    RealRtcBackend rtc(&my_storage);

    PersistentData data;
    memset(&data, 0xAA, sizeof(data));

    // Verify it correctly writes to and reads from the assigned memory.
    TEST_ASSERT_EQUAL(ESP_OK, rtc.save(&data, sizeof(data)));

    PersistentData loaded;
    TEST_ASSERT_EQUAL(ESP_OK, rtc.load(&loaded, sizeof(loaded)));
    TEST_ASSERT_EQUAL_MEMORY(&data, &loaded, sizeof(data));
}

TEST_CASE("RealNvsBackend integration", "[storage_backend]")
{
    // Verification using the real ESP-IDF NVS implementation (host-side mock).
    RealNvsBackend nvs;

    PersistentData data;
    memset(&data, 0, sizeof(data));
    data.magic = 0x12345678;

    // Verify that NVS persistence actually works on the host environment.
    TEST_ASSERT_EQUAL(ESP_OK, nvs.save(&data, sizeof(data)));

    PersistentData loaded;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.load(&loaded, sizeof(loaded)));
    TEST_ASSERT_EQUAL(data.magic, loaded.magic);

    // Cleanup NVS for next tests.
    nvs_flash_deinit();
}
