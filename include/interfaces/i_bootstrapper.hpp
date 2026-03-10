// include/interfaces/i_bootstrapper.hpp
#pragma once

#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "espnow_types.hpp" // EspNowConfig

/**
 * @struct EspNowBootstrapConfig
 * @brief Configuration for bootstrapping the ESP-NOW system
 * @internal
 */
struct EspNowBootstrapConfig
{
    esp_now_recv_cb_t recv_cb;
    esp_now_send_cb_t send_cb;
    TaskFunction_t rx_dispatch_fn;
    TaskFunction_t transport_worker_fn;
    void *task_params;
};

/**
 * @interface IBootstrapper
 * @brief Bootstrapper interface (internal)
 * @internal
 */
class IBootstrapper
{
public:
    virtual ~IBootstrapper() = default;

    virtual esp_err_t init(
        const EspNowConfig &config,
        const EspNowBootstrapConfig &bootstrap_cfg,
        QueueHandle_t &rx_queue,
        QueueHandle_t &worker_queue,
        SemaphoreHandle_t &ack_mutex,
        TaskHandle_t &rx_handle,
        TaskHandle_t &worker_handle) = 0;

    virtual esp_err_t deinit(
        QueueHandle_t &rx_queue,
        QueueHandle_t &worker_queue,
        SemaphoreHandle_t &ack_mutex,
        TaskHandle_t &rx_handle,
        TaskHandle_t &worker_handle) = 0;
};