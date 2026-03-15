// include/bootstrapper.hpp
#pragma once

#include "esp_err.h"

#include "i_bootstrapper.hpp"
#include "i_hal_wifi.hpp"
#include "i_hal_freertos.hpp"
// #include "i_peer_manager.hpp"
// #include "i_tx_manager.hpp"
// #include "i_heartbeat_manager.hpp"

/**
 * @class Bootstrapper
 * @brief Bootstrapper class (internal)
 * @internal
 */
class Bootstrapper : public IBootstrapper
{
public:
    Bootstrapper(IWiFiHAL &wifi_hal, IFreeRTOSHAL &freertos_hal);

    esp_err_t init(
        const EspNowConfig &config,
        const EspNowBootstrapConfig &bootstrap_cfg,
        QueueHandle_t &rx_queue,
        QueueHandle_t &worker_queue,
        SemaphoreHandle_t &ack_mutex,
        TaskHandle_t &rx_handle,
        TaskHandle_t &worker_handle) override;

    esp_err_t deinit(
        QueueHandle_t &rx_queue,
        QueueHandle_t &worker_queue,
        SemaphoreHandle_t &ack_mutex,
        TaskHandle_t &rx_handle,
        TaskHandle_t &worker_handle) override;

private:
    IWiFiHAL &wifi_hal_;
    IFreeRTOSHAL &freertos_hal_;
};