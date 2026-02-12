#include "unity.h"
#include "wifi_hal.hpp"
#include "mock_wifi_hal.hpp"
extern "C" {
#include "Mockesp_wifi.h"
#include "Mockesp_now.h"
}
#include <cstring>

TEST_CASE("RealWiFiHAL set_channel calls esp_wifi_set_channel", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t channel = 5;

    esp_wifi_set_channel_ExpectAndReturn(channel, WIFI_SECOND_CHAN_NONE, ESP_OK);

    esp_err_t err = hal.set_channel(channel);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("RealWiFiHAL get_channel calls esp_wifi_get_channel", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t expected_channel = 7;

    esp_wifi_get_channel_ExpectAnyArgsAndReturn(ESP_OK);
    esp_wifi_get_channel_ReturnThruPtr_primary(&expected_channel);

    uint8_t actual_channel = 0;
    esp_err_t err = hal.get_channel(&actual_channel);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(expected_channel, actual_channel);
}

TEST_CASE("RealWiFiHAL send_packet edge cases (0, 1, 250, 251 bytes)", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    SECTION("Zero bytes - Some drivers return error, others success")
    {
        // Testing with 0 bytes should return ESP_ERR_INVALID_ARG
        esp_now_send_ExpectAndReturn(mac, nullptr, 0, ESP_ERR_INVALID_ARG);
        esp_err_t err = hal.send_packet(mac, nullptr, 0);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    }

    // Testing with 1 byte should return ESP_OK
    SECTION("Minimum payload (1 byte)")
    {
        uint8_t data = 0xFF;
        esp_now_send_ExpectAndReturn(mac, &data, 1, ESP_OK);
        esp_err_t err = hal.send_packet(mac, &data, 1);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    // Testing with 250 bytes sould return ESP_OK
    SECTION("Maximum ESP-NOW payload (250 bytes)")
    {
        uint8_t data[250];
        memset(data, 0xEE, 250);
        esp_now_send_ExpectAndReturn(mac, data, 250, ESP_OK);
        esp_err_t err = hal.send_packet(mac, data, 250);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    // Testing with more than 250 bytes should return ESP_ERR_ESPNOW_ARG
    SECTION("Above limit (251 bytes)")
    {
        uint8_t data[251];
        // IDF should reject and return ESP_ERR_ESPNOW_ARG
        esp_now_send_ExpectAndReturn(mac, data, 251, ESP_ERR_ESPNOW_ARG);
        esp_err_t err = hal.send_packet(mac, data, 251);
        TEST_ASSERT_EQUAL(ESP_ERR_ESPNOW_ARG, err);
    }
}

TEST_CASE("RealWiFiHAL wait_for_event returns false on timeout", "[wifi_hal]")
{
    RealWiFiHAL hal;
    // We expect it to return false because no one is notifying the task
    // Using a small timeout to avoid hanging the test
    bool result = hal.wait_for_event(0x01, 10);
    TEST_ASSERT_FALSE(result);
}

TEST_CASE("MockWiFiHAL event_responses queue works", "[wifi_hal_mock]")
{
    MockWiFiHAL mock;
    mock.event_responses.push(false);
    mock.event_responses.push(true);

    TEST_ASSERT_FALSE(mock.wait_for_event(0x01, 0));
    TEST_ASSERT_TRUE(mock.wait_for_event(0x01, 0));

    // Should fallback to wait_for_event_ret when empty
    mock.wait_for_event_ret = false;
    TEST_ASSERT_FALSE(mock.wait_for_event(0x01, 0));
}
