#include "unity.h"
#include "wifi_hal.hpp"
#include "mock_wifi_hal.hpp"
extern "C" {
#include "Mockesp_wifi.h"
#include "Mockesp_now.h"
}
#include <cstring>

void setUp(void)
{
    Mockesp_wifi_Init();
    Mockesp_now_Init();
}

void tearDown(void)
{
    Mockesp_wifi_Verify();
    Mockesp_wifi_Destroy();
    Mockesp_now_Verify();
    Mockesp_now_Destroy();
}

/**
 * @file test_wifi_hal.cpp
 * @brief Tests for the RealWiFiHAL class, which wraps ESP-IDF WiFi and ESP-NOW APIs.
 */

TEST_CASE("RealWiFiHAL set_channel calls esp_wifi_set_channel", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t channel = 5;

    // We expect the HAL to call the IDF function esp_wifi_set_channel with specific arguments.
    esp_wifi_set_channel_ExpectAndReturn(channel, WIFI_SECOND_CHAN_NONE, ESP_OK);

    esp_err_t err = hal.set_channel(channel);

    // Verify that the error code returned by the HAL is what we stubbed in the mock.
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("RealWiFiHAL get_channel calls esp_wifi_get_channel", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t expected_channel = 7;

    // Stub the IDF function to return a specific channel value via its pointer argument.
    esp_wifi_get_channel_ExpectAnyArgsAndReturn(ESP_OK);
    esp_wifi_get_channel_ReturnThruPtr_primary(&expected_channel);

    uint8_t actual_channel = 0;
    esp_err_t err = hal.get_channel(&actual_channel);

    // Verify the HAL correctly passed back the channel retrieved from the IDF.
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(expected_channel, actual_channel);
}

TEST_CASE("RealWiFiHAL send_packet with zero bytes returns error", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // Testing boundary condition: Sending 0 bytes should be rejected by the IDF.
    esp_now_send_ExpectAndReturn(mac, nullptr, 0, ESP_ERR_INVALID_ARG);

    esp_err_t err = hal.send_packet(mac, nullptr, 0);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

TEST_CASE("RealWiFiHAL send_packet with 1 byte (minimum) returns OK", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t data = 0xFF;

    // Minimum valid payload size for ESP-NOW.
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

    // Maximum valid payload size for ESP-NOW.
    esp_now_send_ExpectAndReturn(mac, data, 250, ESP_OK);

    esp_err_t err = hal.send_packet(mac, data, 250);

    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("RealWiFiHAL send_packet with 251 bytes (above limit) returns error", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t data[251];

    // Boundary condition: Payload exceeds maximum ESP-NOW limit.
    esp_now_send_ExpectAndReturn(mac, data, 251, ESP_ERR_ESPNOW_ARG);

    esp_err_t err = hal.send_packet(mac, data, 251);

    TEST_ASSERT_EQUAL(ESP_ERR_ESPNOW_ARG, err);
}

TEST_CASE("RealWiFiHAL wait_for_event returns false on timeout", "[wifi_hal]")
{
    RealWiFiHAL hal;

    // Verify that wait_for_event correctly times out when no notification bits are set.
    // Using a small timeout (10ms) to keep the test suite fast.
    bool result = hal.wait_for_event(0x01, 10);

    TEST_ASSERT_FALSE(result);
}

TEST_CASE("MockWiFiHAL event_responses queue works", "[wifi_hal_mock]")
{
    // This tests the Mock infrastructure itself, ensuring we can simulate complex
    // asynchronous success/fail sequences.
    MockWiFiHAL mock;

    // Queue two responses: the first call fails, the second succeeds.
    mock.event_responses.push(false);
    mock.event_responses.push(true);

    TEST_ASSERT_FALSE(mock.wait_for_event(0x01, 0)); // Pops false
    TEST_ASSERT_TRUE(mock.wait_for_event(0x01, 0));  // Pops true

    // Should fallback to the static wait_for_event_ret when the queue is empty.
    mock.wait_for_event_ret = false;
    TEST_ASSERT_FALSE(mock.wait_for_event(0x01, 0));
}
