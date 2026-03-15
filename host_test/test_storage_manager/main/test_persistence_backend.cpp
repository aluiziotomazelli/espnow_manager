#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "esp_attr.h"
#include "mock_hal_nvs.hpp"

#include "persistence_backend.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

// ==============================================================================
// NVS Backend
// ==============================================================================

class NvsBackendTest : public ::testing::Test
{
protected:
    NiceMock<MockNvsHAL> nvs_hal;
    NvsBackend nvs{nvs_hal};

    void SetUp() override
    {
        // happy path as default
        ON_CALL(nvs_hal, hal_nvs_flash_init()).WillByDefault(Return(ESP_OK));
        ON_CALL(nvs_hal, hal_nvs_open(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(nvs_hal, hal_nvs_close(_)).WillByDefault(Return());
        ON_CALL(nvs_hal, hal_nvs_get_blob(_, _, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(nvs_hal, hal_nvs_set_blob(_, _, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(nvs_hal, hal_nvs_commit(_)).WillByDefault(Return(ESP_OK));
    }
};

// Test nvs_init
TEST_F(NvsBackendTest, NvsFlahsInitFailPropagatesError)
{
    EXPECT_CALL(nvs_hal, hal_nvs_flash_init()).WillOnce(Return(ESP_FAIL)); // NVS init fails

    PersistentData data = {};
    EXPECT_EQ(ESP_FAIL, nvs.save(&data, sizeof(PersistentData))); // Returns error
}

TEST_F(NvsBackendTest, NvsFlahsInitWithMoFreePagesCallsEraseandInit)
{
    EXPECT_CALL(nvs_hal, hal_nvs_flash_init())
        .Times(2)
        .WillOnce(Return(ESP_ERR_NVS_NO_FREE_PAGES))      // First call returns ESP_ERR_NVS_NO_FREE_PAGES
        .WillOnce(Return(ESP_OK));                        // Second call returns ESP_OK
    EXPECT_CALL(nvs_hal, hal_nvs_flash_erase()).Times(1); // Must call erase

    PersistentData data = {};
    EXPECT_EQ(ESP_OK, nvs.save(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, NvsFlahsInitWithNewVersionCallsEraseandInit)
{
    EXPECT_CALL(nvs_hal, hal_nvs_flash_init())
        .Times(2)
        .WillOnce(Return(ESP_ERR_NVS_NEW_VERSION_FOUND))  // First call returns ESP_ERR_NVS_NEW_VERSION_FOUND
        .WillOnce(Return(ESP_OK));                        // Second call returns ESP_OK
    EXPECT_CALL(nvs_hal, hal_nvs_flash_erase()).Times(1); // Must call erase

    PersistentData data = {};
    EXPECT_EQ(ESP_OK, nvs.save(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, NvsFlahsInitFailsTwoTimesPropagatesError)
{
    EXPECT_CALL(nvs_hal, hal_nvs_flash_init())
        .Times(2)
        .WillOnce(Return(ESP_ERR_NVS_NEW_VERSION_FOUND))  // First call returns ESP_ERR_NVS_NEW_VERSION_FOUND
        .WillOnce(Return(ESP_FAIL));                      // Second call returns ESP_FAIL
    EXPECT_CALL(nvs_hal, hal_nvs_flash_erase()).Times(1); // Must call erase

    PersistentData data = {};
    EXPECT_EQ(ESP_FAIL, nvs.save(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, SecondSaveSkipsFlashInit)
{
    // Firts save - nvs_initialized_ = false
    EXPECT_CALL(nvs_hal, hal_nvs_flash_init()).Times(1); // Must call hal_nvs_flash_init
    PersistentData data = {};
    nvs.save(&data, sizeof(PersistentData));

    // Second save - nvs_initialized_ = true
    EXPECT_CALL(nvs_hal, hal_nvs_flash_init()).Times(0); // Must not call hal_nvs_flash_init
    nvs.save(&data, sizeof(PersistentData));
}

// Test NVS Save
TEST_F(NvsBackendTest, SaveFailsToOpenNvsPropagatesError)
{
    ON_CALL(nvs_hal, hal_nvs_open(_, _, _)).WillByDefault(Return(ESP_FAIL)); // NVS open fails
    EXPECT_CALL(nvs_hal, hal_nvs_set_blob(_, _, _, _)).Times(0);             // Must not call hal_nvs_set_blob)

    PersistentData data = {};
    EXPECT_EQ(ESP_FAIL, nvs.save(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, SaveFailsToSetBlobPropagatesError)
{
    ON_CALL(nvs_hal, hal_nvs_set_blob(_, _, _, _)).WillByDefault(Return(ESP_FAIL)); // Returns ESP_FAIL
    EXPECT_CALL(nvs_hal, hal_nvs_commit(_)).Times(0);                               // Must not call hal_nvs_commit

    PersistentData data = {};
    EXPECT_EQ(ESP_FAIL, nvs.save(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, SaveFailsToCommitPropagatesError)
{
    ON_CALL(nvs_hal, hal_nvs_commit(_)).WillByDefault(Return(ESP_FAIL)); // Returns ESP_FAIL

    PersistentData data = {};
    EXPECT_EQ(ESP_FAIL, nvs.save(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, SaveReturnsSuccess)
{
    EXPECT_CALL(nvs_hal, hal_nvs_open(_, _, _)).Times(1);
    EXPECT_CALL(nvs_hal, hal_nvs_set_blob(_, _, _, _)).Times(1);
    EXPECT_CALL(nvs_hal, hal_nvs_commit(_)).Times(1);
    EXPECT_CALL(nvs_hal, hal_nvs_close(_)).Times(1);

    PersistentData data = {};
    EXPECT_EQ(ESP_OK, nvs.save(&data, sizeof(PersistentData)));
}

// Load Tests
TEST_F(NvsBackendTest, LoadReturnsSuccess)
{
    PersistentData data = {};
    EXPECT_EQ(ESP_OK, nvs.load(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, LoadReturnsErroWhenNvsInitFails)
{
    ON_CALL(nvs_hal, hal_nvs_flash_init()).WillByDefault(Return(ESP_FAIL)); // NVS init fails
    EXPECT_CALL(nvs_hal, hal_nvs_open(_, _, _)).Times(0);                   // Must not call hal_nvs_open

    PersistentData data = {};
    EXPECT_EQ(ESP_FAIL, nvs.load(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, LoadReturnsErroWhenNvsOpenFails)
{
    ON_CALL(nvs_hal, hal_nvs_open(_, _, _)).WillByDefault(Return(ESP_FAIL)); // NVS open fails
    EXPECT_CALL(nvs_hal, hal_nvs_get_blob(_, _, _, _)).Times(0);             // Must not call hal_nvs_get_blob

    PersistentData data = {};
    EXPECT_EQ(ESP_FAIL, nvs.load(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, LoadReturnsErroWhenNvsGetBlobFails)
{
    ON_CALL(nvs_hal, hal_nvs_get_blob(_, _, _, _)).WillByDefault(Return(ESP_FAIL)); // NVS get blob fails

    PersistentData data = {}; // NVS get blob fails
    EXPECT_EQ(ESP_FAIL, nvs.load(&data, sizeof(PersistentData)));
}

TEST_F(NvsBackendTest, LoadReturnsErrorWhenSizeMismatch)
{
    size_t wrong_size = sizeof(PersistentData) - 1;

    ON_CALL(nvs_hal, hal_nvs_get_blob(_, _, _, _))
        .WillByDefault(DoAll(
            SetArgPointee<3>(wrong_size), // NVS get blob returns wrong size
            Return(ESP_OK)));             // NVS get blob returns ESP_OK

    PersistentData data = {};
    EXPECT_EQ(ESP_ERR_INVALID_SIZE, nvs.load(&data, sizeof(PersistentData)));
}

// ==============================================================================
// RTC Backend
// ==============================================================================

// Although RTC_DATA_ATTR is not critical in test environment, it is used for consistency with production code
static RTC_DATA_ATTR PersistentData g_rtc_storage;

class RtcBackendTest : public ::testing::Test
{
protected:
    // For testing purposes, we can use local storage instead of RTC
    // PersistentData storage = {};  // local storage for RTC
    // RtcBackend backend{&storage}; // storage injection

    RtcBackend backend{g_rtc_storage}; // storage injection
};

TEST_F(RtcBackendTest, SaveAndLoadRoundtrip)
{
    PersistentData original = {};
    original.magic = PersistentData::MAGIC;
    original.version = PersistentData::VERSION;
    original.wifi_channel = 7;

    EXPECT_EQ(ESP_OK, backend.save(&original, sizeof(PersistentData)));

    PersistentData loaded = {};
    EXPECT_EQ(ESP_OK, backend.load(&loaded, sizeof(PersistentData)));

    EXPECT_EQ(loaded.magic, original.magic);
    EXPECT_EQ(loaded.wifi_channel, original.wifi_channel);
}

TEST_F(RtcBackendTest, LoadReturnsSizeErrorWhenTooLarge)
{
    PersistentData data = {};
    EXPECT_EQ(ESP_ERR_INVALID_SIZE, backend.load(&data, sizeof(PersistentData) + 1));
}

TEST_F(RtcBackendTest, SaveReturnsSizeErrorWhenTooLarge)
{
    PersistentData data = {};
    EXPECT_EQ(ESP_ERR_INVALID_SIZE, backend.save(&data, sizeof(PersistentData) + 1));
}
