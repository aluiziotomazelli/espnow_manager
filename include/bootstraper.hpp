// include/bootstraper.hpp
#pragma once

#include "esp_err.h"

#include "i_bootstraper.hpp"
#include "i_wifi_hal.hpp"
#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_heartbeat_manager.hpp"

/**
 * @class Bootstraper
 * @brief Bootstraper class (internal)
 * @internal
 */
class Bootstraper : public IBootstraper
{
public:
    ~Bootstraper();
    esp_err_t init() override;
    esp_err_t deinit() override;
};