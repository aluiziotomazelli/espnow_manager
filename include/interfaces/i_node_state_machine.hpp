#pragma once

#include "esp_err.h"
#include "espnow_types.hpp"

/**
 * @brief Enum to represent the action to take when a channel is found.
 */
enum class ChannelFoundAction
{
    START_PAIRING,
    STORE_CHANNEL,
    NOTHING
};

/**
 * @brief Interface for the Node State Machine.
 *
 * This class governs the high-level state of the ESP-NOW node (Peripheral or HUB).
 */
class INodeStateMachine
{
public:
    virtual ~INodeStateMachine() = default;

    /**
     * @brief Gets the current state of the node.
     * @return Current NodeState.
     */
    virtual NodeState get_state() const = 0;

    /**
     * @brief Resets the state machine to UNINITIALIZED.
     */
    virtual void reset() = 0;

    /**
     * @brief Event: Initialization successful.
     * @param has_peers True if peers were loaded from storage.
     * @return ESP_OK or ESP_ERR_INVALID_STATE.
     */
    virtual esp_err_t on_init(bool has_peers) = 0;

    /**
     * @brief Event: Deinitialization requested.
     * @return ESP_OK.
     */
    virtual esp_err_t on_deinit() = 0;

    /**
     * @brief Event: Pairing process requested by user/app.
     * @param has_peers True if the node already has peers.
     * @return ESP_OK or ESP_ERR_INVALID_STATE.
     */
    virtual esp_err_t on_pairing_requested(bool has_peers) = 0;

    /**
     * @brief Event: Pairing process completed (either success or timeout).
     * @param has_peers True if the node still has known peers.
     * @return ESP_OK or ESP_ERR_INVALID_STATE.
     */
    virtual esp_err_t on_pairing_timeout(bool has_peers) = 0;

    /**
     * @brief Event: Channel scan requested (typically by TxManager on failure).
     * @return ESP_OK or ESP_ERR_INVALID_STATE.
     */
    virtual esp_err_t on_scan_requested() = 0;

    /**
     * @brief Event: Channel rediscovered.
     * @return ESP_OK or ESP_ERR_INVALID_STATE.
     */
    virtual esp_err_t on_channel_found() = 0;

    /**
     * @brief Event: Channel scan failed after all attempts.
     * @param has_peers True if the node has known peers.
     * @return ESP_OK or ESP_ERR_INVALID_STATE.
     */
    virtual esp_err_t on_scan_failed(bool has_peers) = 0;
};
