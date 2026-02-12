#include "unity.h"
#include "wifi_hal.hpp"
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

TEST_CASE("RealWiFiHAL send_packet calls esp_now_send", "[wifi_hal]")
{
    RealWiFiHAL hal;
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t data[] = {0x01, 0x02, 0x03};
    size_t len = sizeof(data);

    esp_now_send_ExpectAndReturn(mac, data, len, ESP_OK);

    esp_err_t err = hal.send_packet(mac, data, len);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("RealWiFiHAL wait_for_event returns false on timeout", "[wifi_hal]")
{
    RealWiFiHAL hal;
    // We expect it to return false because no one is notifying the task
    // Using a small timeout to avoid hanging the test
    bool result = hal.wait_for_event(0x01, 10);
    TEST_ASSERT_FALSE(result);
}
