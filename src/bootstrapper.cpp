// src/bootstrapper.cpp

#include "esp_log.h"

#include "bootstrapper.hpp"

const char *TAG = "Bootstrapper";

static constexpr uint8_t RX_QUEUE_SIZE = 30;
static constexpr uint8_t WORKER_QUEUE_SIZE = 20;

Bootstrapper::Bootstrapper(IWiFiHAL &wifi_hal, IFreeRTOSHAL &freertos_hal)
    : wifi_hal_(wifi_hal)
    , freertos_hal_(freertos_hal)

{
}

esp_err_t Bootstrapper::init(
    const EspNowConfig &config,
    const EspNowBootstrapConfig &bootstrap_cfg,
    QueueHandle_t &rx_queue,
    QueueHandle_t &worker_queue,
    SemaphoreHandle_t &ack_mutex,
    TaskHandle_t &rx_handle,
    TaskHandle_t &worker_handle)
{
    esp_err_t err;

    wifi_mode_t mode;
    err = wifi_hal_.wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        return err;
    }
    if (mode == WIFI_MODE_NULL) {
        ESP_LOGE(TAG, "Mode is not supported: %d", mode);
        return ESP_ERR_INVALID_STATE;
    }

    err = wifi_hal_.hal_esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(err));
        return err;
    }

    err = wifi_hal_.hal_espnow_register_recv_cb(bootstrap_cfg.recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ESP-NOW receive callback: %s", esp_err_to_name(err));
        return err;
    }

    err = wifi_hal_.hal_espnow_register_send_cb(bootstrap_cfg.send_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ESP-NOW send callback: %s", esp_err_to_name(err));
        return err;
    }

    err = wifi_hal_.wifi_set_channel(config.wifi_channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi channel: %s", esp_err_to_name(err));
        return err;
    }

    {
        esp_now_peer_info_t broadcast_peer = {};
        const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
        broadcast_peer.channel = config.wifi_channel;
        broadcast_peer.ifidx = WIFI_IF_STA;
        broadcast_peer.encrypt = false;
        err = wifi_hal_.hal_esp_now_add_peer(&broadcast_peer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_add_peer (broadcast) failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    ack_mutex = freertos_hal_.mutex_create();
    if (ack_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create ack mutex");
        return ESP_ERR_NO_MEM;
    }

    rx_queue = freertos_hal_.queue_create(RX_QUEUE_SIZE, sizeof(RxPacket));
    worker_queue = freertos_hal_.queue_create(WORKER_QUEUE_SIZE, sizeof(RxPacket));
    if (rx_queue == nullptr || worker_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create queues");
        return ESP_ERR_NO_MEM;
    }

    // Use the injected task function pointer instead of a hardcoded symbol
    BaseType_t task_rx = freertos_hal_.task_create(
        bootstrap_cfg.rx_dispatch_fn,
        "rx_dispatch_task",
        config.stack_size_rx_dispatch,
        bootstrap_cfg.task_params,
        config.priority_rx_dispatch,
        &rx_handle);
    if (task_rx != pdPASS) {
        ESP_LOGE(TAG, "Failed to create rx dispatch task");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_worker = freertos_hal_.task_create(
        bootstrap_cfg.transport_worker_fn,
        "worker_task",
        config.stack_size_transport_worker,
        bootstrap_cfg.task_params,
        config.priority_transport_worker,
        &worker_handle);
    if (task_worker != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Bootstrapper::deinit(
    QueueHandle_t &rx_queue,
    QueueHandle_t &worker_queue,
    SemaphoreHandle_t &ack_mutex,
    TaskHandle_t &rx_handle,
    TaskHandle_t &worker_handle)
{
    // Delete tasks
    if (rx_handle != nullptr) {
        freertos_hal_.task_delete(rx_handle);
        ESP_LOGW(TAG, "Deleting rx dispatch task");

        rx_handle = nullptr;
    }
    if (worker_handle != nullptr) {
        freertos_hal_.task_delete(worker_handle);
        ESP_LOGW(TAG, "Deleting worker task");
        worker_handle = nullptr;
    }

    // Delete queues
    if (rx_queue != nullptr) {
        freertos_hal_.queue_delete(rx_queue);
        rx_queue = nullptr;
    }
    if (worker_queue != nullptr) {
        freertos_hal_.queue_delete(worker_queue);
        worker_queue = nullptr;
    }

    // Delete mutex
    if (ack_mutex != nullptr) {
        freertos_hal_.semaphore_delete(ack_mutex);
        ack_mutex = nullptr;
    }

    return wifi_hal_.hal_esp_now_deinit();
}