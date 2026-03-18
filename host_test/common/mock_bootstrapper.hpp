// host_test/common/mock_bootstrapper.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_bootstrapper.hpp"

class MockBootstrapper : public IBootstrapper
{
public:
    MOCK_METHOD(
        esp_err_t,
        init,
        (const EspNowConfig &config,
         const EspNowBootstrapConfig &bootstrap_cfg,
         QueueHandle_t &rx_queue,
         QueueHandle_t &worker_queue,
         SemaphoreHandle_t &ack_mutex,
         TaskHandle_t &rx_handle,
         TaskHandle_t &worker_handle),
        (override));

    MOCK_METHOD(
        esp_err_t,
        deinit,
        (QueueHandle_t & rx_queue,
         QueueHandle_t & worker_queue,
         SemaphoreHandle_t & ack_mutex,
         TaskHandle_t & rx_handle,
         TaskHandle_t & worker_handle),
        (override));
};
