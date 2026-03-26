// src/discovery_manager.cpp
#include <cstring>

#include "esp_log.h"

#include "discovery_manager.hpp"
#include "protocol_types.hpp"

static const char* TAG = "DiscoveryMgr";

DiscoveryManager::DiscoveryManager(IWiFiHAL& wifi_hal, IMessageCodec& message_codec, IFreeRTOSHAL& freertos_hal)
    : hal_wifi_(wifi_hal)
    , message_codec_(message_codec)
    , hal_freertos_(freertos_hal)
{
}

// TODO: replace params with EspNowConfig?
esp_err_t
DiscoveryManager::init(NodeId id, NodeType type, TaskHandle_t rx_task_handle, UBaseType_t priority, uint32_t stack_size)
{
    if (rx_task_handle == nullptr) {
        ESP_LOGE(TAG, "RX task handle is required for non-Hub type.");
        return ESP_ERR_INVALID_ARG;
    }
    rx_task_handle_ = rx_task_handle;

    BaseType_t ret;
    ret = hal_freertos_.task_create(
        discovery_task_func, "discovery_task", stack_size, this, priority, &discovery_task_handle_);

    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    my_node_id_ = id;
    my_node_type_ = type;

    if (type == ReservedTypes::HUB) {
        hub_ready_ = true;
    }
    else {
        node_ready_ = true;
    }

    return ESP_OK;
}

void DiscoveryManager::handle_scan_probe(const DecodedPacket& decoded)
{
    if (!hub_ready_) {
        ESP_LOGE(TAG, "DiscoveryManager not initialized properly. Call init() before handle_probe().");
        return;
    }

    destination_node_id_ = decoded.header.sender_node_id;
    hal_freertos_.task_notify(discovery_task_handle_, NOTIFY_SCAN_RESPONSE, eSetBits);
}

void DiscoveryManager::set_channel(uint8_t channel)
{
    current_channel_ = channel;
}

void DiscoveryManager::discovery_task_func(void* arg)
{
    DiscoveryManager* self = static_cast<DiscoveryManager*>(arg);
    self->discovery_task();
}

void DiscoveryManager::discovery_task()
{
    bool should_stop = false;
    uint32_t notifications = 0;

    while (!should_stop) {
        // Wait blocked for any notification
        if (hal_freertos_.task_notify_wait(0, NOTIFY_ALL, &notifications, portMAX_DELAY) == pdPASS) {
            if ((notifications & NOTIFY_STOP) == NOTIFY_STOP) {
                should_stop = true;
            }
            if ((notifications & NOTIFY_START_SCAN) == NOTIFY_START_SCAN) {
                if (scan_channel() == ESP_OK) {
                    notify_rx_task(NOTIFY_CHANNEL_FOUND);
                }
                else {
                    notify_rx_task(NOTIFY_SCAN_FAILED);
                }
            }
            if ((notifications & NOTIFY_SCAN_RESPONSE) == NOTIFY_SCAN_RESPONSE) {
                send_scan_response();
            }
        }
    }

    ESP_LOGI(TAG, "Discovery Manager task exiting.");
    discovery_task_handle_ = nullptr;
    hal_freertos_.task_suspend(nullptr); // NULL / nullptr == current task
    hal_freertos_.task_delete(nullptr);  // NULL / nullptr == current task
}

// ==========================================================================
// Private methods
// ==========================================================================

esp_err_t DiscoveryManager::scan_channel()
{
    if (!node_ready_) {
        ESP_LOGE(TAG, "DiscoveryManager not initialized properly. Call init() before scan().");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting channel scan to find Hub.");
    esp_err_t ret = ESP_FAIL;

    // We will start from actual channel (most likely to be the correct one)
    for (uint8_t offset = 0; offset < 13 && ret != ESP_OK; ++offset) {
        uint8_t channel = ((current_channel_ - 1 + offset) % 13) + 1;
        ESP_LOGD(TAG, "Scanning channel %d", channel);

        // Set wifi channel to make probes attempts on this channel
        ret = hal_wifi_.wifi_set_channel(channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi channel to %d: %s", channel, esp_err_to_name(ret));
            continue;
        }

        // We made SCAN_CHANNEL_ATTEMPTS per channel
        for (uint8_t attempt = 0; attempt < SCAN_CHANNEL_ATTEMPTS && ret != ESP_OK; attempt++) {
            // Send probe on channel
            ret = send_scan_probe();
            if (ret != ESP_OK) {
                continue;
            }

            // Wait for hub to respond
            if (hub_was_found()) {
                current_channel_ = channel;
                ESP_LOGI(TAG, "Hub found on channel %d", channel);
                ret = ESP_OK;
                break;
            }
        }
    }
    return ret;
}

esp_err_t DiscoveryManager::send_scan_probe()
{
    MessageHeader probe_header = make_probe_header();
    uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
    size_t encoded_len = message_codec_.encode(probe_header, nullptr, 0, buffer, sizeof(buffer));
    if (encoded_len == 0) {
        return ESP_FAIL;
    }

    return hal_wifi_.hal_esp_now_send(BROADCAST_MAC, buffer, encoded_len);
}

MessageHeader DiscoveryManager::make_probe_header()
{
    // empty initializer to avoid memory garbage on unused fields
    MessageHeader resp = {};
    resp.msg_type = MessageType::CHANNEL_SCAN_RESPONSE;
    resp.sender_node_id = my_node_id_;
    resp.sender_type = my_node_type_;
    resp.dest_node_id = ReservedIds::HUB;

    return resp;
}

bool DiscoveryManager::hub_was_found()
{
    uint32_t notifications = 0;
    if (hal_freertos_.task_notify_wait(0, NOTIFY_LINK_ALIVE, &notifications, pdMS_TO_TICKS(SCAN_CHANNEL_TIMEOUT_MS)) ==
        pdPASS) {
        if ((notifications & NOTIFY_LINK_ALIVE) == NOTIFY_LINK_ALIVE) {
            return true;
        }
    }
    return false;
}

void DiscoveryManager::notify_rx_task(uint32_t notification)
{
    hal_freertos_.task_notify(rx_task_handle_, notification, eSetBits);
}

esp_err_t DiscoveryManager::send_scan_response()
{
    ESP_LOGD(TAG, "Sending channel scan response to node_id=%d", destination_node_id_);

    MessageHeader response_header = make_response_header();

    uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
    size_t encoded_len = message_codec_.encode(response_header, nullptr, 0, buffer, sizeof(buffer));
    if (encoded_len == 0) {
        return ESP_FAIL;
    }

    return hal_wifi_.hal_esp_now_send(BROADCAST_MAC, buffer, encoded_len);
}

MessageHeader DiscoveryManager::make_response_header()
{
    MessageHeader resp = {};
    resp.msg_type = MessageType::CHANNEL_SCAN_RESPONSE;
    resp.sender_node_id = my_node_id_;
    resp.sender_type = my_node_type_;
    resp.dest_node_id = destination_node_id_;

    return resp;
}