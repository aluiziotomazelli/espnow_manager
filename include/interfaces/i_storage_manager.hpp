// include/interfaces/i_storage_manager.hpp
#pragma once

#include <cstdint>

#include "etl/vector.h"

#include "esp_err.h"

#include "espnow_types.hpp"

/**
 * @interface IStorageManager
 * @brief Higher-level storage management for peers and config (internal)
 * @internal
 */
class IStorageManager
{
public:
    virtual ~IStorageManager() = default;

    /** @internal */
    virtual esp_err_t load(uint8_t &wifi_channel, etl::ivector<PersistentPeer> &peers) = 0;
    /** @internal */
    virtual esp_err_t
    save(uint8_t wifi_channel, const etl::ivector<PersistentPeer> &peers, bool force_nvs_commit = true) = 0;
};
