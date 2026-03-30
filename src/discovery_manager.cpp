// src/discovery_manager.cpp
#include <cstring>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "discovery_manager.hpp"
#include "protocol_types.hpp"

static const char* TAG = "DiscoveryMgr";

DiscoveryManager::DiscoveryManager(
    IWiFiHAL& wifi_hal,
    IEspNowHAL& espnow_hal,
    IMessageCodec& message_codec,
    IFreeRTOSHAL& freertos_hal)
    : hal_wifi_(wifi_hal)
    , hal_espnow_(espnow_hal)
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

void DiscoveryManager::deinit()
{
    // Wake task from any blocking state
    if (discovery_task_handle_ != nullptr) {
        hal_freertos_.task_notify(discovery_task_handle_, NOTIFY_STOP | NOTIFY_STOP_SCAN, eSetBits);
    }

    // Wait for task to exit (worst case: full scan cycle)
    const constexpr uint16_t timeout_ms = SCAN_CHANNEL_TIMEOUT_MS * SCAN_CHANNEL_ATTEMPTS * 13 + 100;
    const constexpr uint8_t delay_ms = 50;
    for (int elapsed_ms = 0; elapsed_ms < timeout_ms; elapsed_ms += delay_ms) {
        if (discovery_task_handle_ == nullptr) {
            break;
        }
        hal_freertos_.task_delay(pdMS_TO_TICKS(delay_ms));
    }

    // Force cleanup if task didn't exit gracefully
    if (discovery_task_handle_ != nullptr) {
        hal_freertos_.task_suspend(discovery_task_handle_);
        hal_freertos_.task_delete(discovery_task_handle_);
        discovery_task_handle_ = nullptr;
    }
}

void DiscoveryManager::start_scan()
{
    if (!node_ready_ || discovery_task_handle_ == nullptr) {
        ESP_LOGE(TAG, "DiscoveryManager not initialized properly. Call init() before start_scan().");
        return;
    }
    hal_freertos_.task_notify(discovery_task_handle_, NOTIFY_START_SCAN, eSetBits);
}

void DiscoveryManager::stop_scan()
{
    if (!node_ready_ || discovery_task_handle_ == nullptr) {
        ESP_LOGE(TAG, "DiscoveryManager not initialized properly. Call init() before stop_scan().");
        return;
    }
    hal_freertos_.task_notify(discovery_task_handle_, NOTIFY_STOP_SCAN, eSetBits);
}

void DiscoveryManager::handle_scan_probe(const DecodedRxPacket& decoded)
{
    if (!hub_ready_ || discovery_task_handle_ == nullptr) {
        ESP_LOGE(TAG, "DiscoveryManager not initialized properly. Call init() before handle_probe().");
        return;
    }

    destination_node_id_ = decoded.header.sender_node_id;
    hal_freertos_.task_notify(discovery_task_handle_, NOTIFY_SCAN_RESPONSE, eSetBits);
}

void DiscoveryManager::set_channel(uint8_t channel)
{
    if (channel >= 1 && channel <= 13) {
        current_channel_.store(channel);
    }
    else {
        ESP_LOGW(TAG, "Invalid channel %d, must be 1-13", channel);
    }
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
        hal_freertos_.task_notify_wait(0, NOTIFY_ALL, &notifications, portMAX_DELAY);

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
    ESP_LOGI(TAG, "Starting channel scan to find Hub.");
    is_scanning_.store(true);
    esp_err_t ret = ESP_FAIL;

    // We will start from actual channel (most likely to be the correct one)
    for (uint8_t offset = 0; offset < 13 && ret != ESP_OK; ++offset) {
        uint8_t channel = ((current_channel_.load() - 1 + offset) % 13) + 1;
        ESP_LOGD(TAG, "Scanning channel %d", channel);

        // Set wifi channel to make probes attempts on this channel
        if (hal_wifi_.wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi channel to %d", channel);
            continue;
        }

        // We made SCAN_CHANNEL_ATTEMPTS per channel
        for (uint8_t attempt = 0; attempt < SCAN_CHANNEL_ATTEMPTS && ret != ESP_OK; attempt++) {
            // Send probe on channel
            if (send_scan_probe() != ESP_OK) {
                continue;
            }

            // Verify if scan should stop
            if (should_stop_scan()) {
                is_scanning_.store(false);
                return ESP_FAIL;
            }

            // Wait for hub to respond
            if (hub_was_found()) {
                current_channel_.store(channel);
                ESP_LOGI(TAG, "Hub found on channel %d", channel);
                ret = ESP_OK;
                is_scanning_.store(false);
                break;
            }
        }
    }
    is_scanning_.store(false);
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

    return hal_espnow_.hal_esp_now_send(BROADCAST_MAC, buffer, encoded_len);
}

MessageHeader DiscoveryManager::make_probe_header()
{
    // empty initializer to avoid memory garbage on unused fields
    MessageHeader resp = {};
    resp.msg_type = MessageType::CHANNEL_SCAN_PROBE;
    resp.sender_node_id = my_node_id_;
    resp.sender_type = my_node_type_;
    resp.dest_node_id = ReservedIds::HUB;

    return resp;
}

bool DiscoveryManager::hub_was_found()
{
    uint32_t notifications = 0;
    hal_freertos_.task_notify_wait(0, NOTIFY_SCAN_RESPONSE, &notifications, pdMS_TO_TICKS(SCAN_CHANNEL_TIMEOUT_MS));
    return (notifications & NOTIFY_SCAN_RESPONSE) == NOTIFY_SCAN_RESPONSE;
}

bool DiscoveryManager::should_stop_scan()
{
    uint32_t notifications = 0;
    hal_freertos_.task_notify_wait(0, NOTIFY_STOP_SCAN, &notifications, 0);
    return (notifications & NOTIFY_STOP_SCAN) == NOTIFY_STOP_SCAN;
}

void DiscoveryManager::notify_rx_task(uint32_t notification)
{
    if (rx_task_handle_ != nullptr) {
        hal_freertos_.task_notify(rx_task_handle_, notification, eSetBits);
    }
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

    // Send via broadcast — probing node maybe is not yet a registered ESP-NOW peer
    return hal_espnow_.hal_esp_now_send(BROADCAST_MAC, buffer, encoded_len);
}

void DiscoveryManager::handle_scan_response(const DecodedRxPacket& decoded)
{
    if (!node_ready_ || discovery_task_handle_ == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "Scan response received, notifying discovery task");
    hal_freertos_.task_notify(discovery_task_handle_, NOTIFY_SCAN_RESPONSE, eSetBits);
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