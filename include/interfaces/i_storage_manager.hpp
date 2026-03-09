// include/internface/i_storage_manager.hpp
#pragma once

#include <cstdint>
#include <vector>

#include "esp_err.h"

#include "espnow_types.hpp"

/**
 * @interface IStorage
 * @brief Higher-level storage management for peers and config (internal)
 * @internal
 */
class IStorage
{
public:
    virtual ~IStorage() = default;

    /** @internal */
    virtual esp_err_t load(uint8_t &wifi_channel, std::vector<PersistentPeer> &peers) = 0;
    /** @internal */
    virtual esp_err_t
    save(uint8_t wifi_channel, const std::vector<PersistentPeer> &peers, bool force_nvs_commit = true) = 0;
};