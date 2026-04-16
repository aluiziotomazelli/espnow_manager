// include/hal_espnow.hpp
#pragma once

#include "esp_now.h"
#include "i_hal_espnow.hpp"

/**
 * @brief Hardware Abstraction Layer implementation for ESP-NOW driver.
 * @internal
 */
class EspNowHAL : public IEspNowHAL
{
public:
    EspNowHAL() = default;

    /** @copydoc IEspNowHAL::hal_esp_now_init() */
    esp_err_t hal_esp_now_init() override { return esp_now_init(); }

    /** @copydoc IEspNowHAL::hal_esp_now_deinit() */
    esp_err_t hal_esp_now_deinit() override { return esp_now_deinit(); }

    /** @copydoc IEspNowHAL::hal_espnow_register_recv_cb() */
    esp_err_t hal_espnow_register_recv_cb(esp_now_recv_cb_t cb) override
    {
        return esp_now_register_recv_cb(cb);
    }

    /** @copydoc IEspNowHAL::hal_espnow_register_send_cb() */
    esp_err_t hal_espnow_register_send_cb(esp_now_send_cb_t cb) override
    {
        return esp_now_register_send_cb(cb);
    }

    /** @copydoc IEspNowHAL::hal_esp_now_add_peer() */
    esp_err_t hal_esp_now_add_peer(const esp_now_peer_info_t *peer) override
    {
        return esp_now_add_peer(peer);
    }

    /** @copydoc IEspNowHAL::hal_esp_now_mod_peer() */
    esp_err_t hal_esp_now_mod_peer(const esp_now_peer_info_t *peer) override
    {
        return esp_now_mod_peer(peer);
    }

    /** @copydoc IEspNowHAL::hal_esp_now_del_peer() */
    esp_err_t hal_esp_now_del_peer(const uint8_t *peer_addr) override
    {
        return esp_now_del_peer(peer_addr);
    }

    /** @copydoc IEspNowHAL::hal_esp_now_send() */
    esp_err_t hal_esp_now_send(const uint8_t *dest_mac, const uint8_t *data, size_t len) override
    {
        return esp_now_send(dest_mac, data, len);
    }
};
