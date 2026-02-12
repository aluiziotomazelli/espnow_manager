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

TEST_CASE("RealWiFiHAL send_packet with zero bytes returns error", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    // Testing with 0 bytes should return ESP_ERR_INVALID_ARG
    esp_now_send_ExpectAndReturn(mac, nullptr, 0, ESP_ERR_INVALID_ARG);
    esp_err_t err = hal.send_packet(mac, nullptr, 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

TEST_CASE("RealWiFiHAL send_packet with 1 byte (minimum) returns OK", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t data = 0xFF;
    esp_now_send_ExpectAndReturn(mac, &data, 1, ESP_OK);
    esp_err_t err = hal.send_packet(mac, &data, 1);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("RealWiFiHAL send_packet with 250 bytes (maximum) returns OK", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t data[250];
    memset(data, 0xEE, 250);
    esp_now_send_ExpectAndReturn(mac, data, 250, ESP_OK);
    esp_err_t err = hal.send_packet(mac, data, 250);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("RealWiFiHAL send_packet with 251 bytes (above limit) returns error", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t data[251];
    // IDF should reject and return ESP_ERR_ESPNOW_ARG
    esp_now_send_ExpectAndReturn(mac, data, 251, ESP_ERR_ESPNOW_ARG);
    esp_err_t err = hal.send_packet(mac, data, 251);
    TEST_ASSERT_EQUAL(ESP_ERR_ESPNOW_ARG, err);
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
