// include/interfaces/i_hal_espnow.hpp
#pragma once

#include "esp_err.h"
#include "esp_now.h"
namespace espnow {

/**
 * @interface IEspNowHAL
 * @brief Hardware Abstraction Layer for ESP-NOW driver.
 * @internal
 */
class IEspNowHAL
{
public:
    virtual ~IEspNowHAL() = default;

    /** @copydoc esp_now_init() */
    virtual esp_err_t hal_esp_now_init() = 0;

    /** @copydoc esp_now_deinit() */
    virtual esp_err_t hal_esp_now_deinit() = 0;

    /** @copydoc esp_now_register_recv_cb() */
    virtual esp_err_t hal_espnow_register_recv_cb(esp_now_recv_cb_t cb) = 0;

    /** @copydoc esp_now_register_send_cb() */
    virtual esp_err_t hal_espnow_register_send_cb(esp_now_send_cb_t cb) = 0;

    /** @copydoc esp_now_add_peer() */
    virtual esp_err_t hal_esp_now_add_peer(const esp_now_peer_info_t *peer) = 0;

    /** @copydoc esp_now_mod_peer() */
    virtual esp_err_t hal_esp_now_mod_peer(const esp_now_peer_info_t *peer) = 0;

    /** @copydoc esp_now_del_peer() */
    virtual esp_err_t hal_esp_now_del_peer(const uint8_t *peer_addr) = 0;

    /** @copydoc esp_now_send() */
    virtual esp_err_t hal_esp_now_send(const uint8_t *dest_mac, const uint8_t *data, size_t len) = 0;
};

} // namespace espnow
