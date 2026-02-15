#pragma once

#include "espnow_interfaces.hpp"
#include "espnow_storage.hpp"

/**
 * @brief Default RTC backend that uses a static PersistentData variable.
 * On real hardware, this variable is placed in RTC slow memory.
 */
class RealRtcBackend : public IPersistenceBackend
{
public:
    RealRtcBackend(PersistentData *storage_ptr = nullptr);

    esp_err_t load(void *data, size_t size) override;
    esp_err_t save(const void *data, size_t size) override;

private:
    PersistentData *storage_;
};

/**
 * @brief Default NVS backend that uses the nvs_flash component.
 */
class RealNvsBackend : public IPersistenceBackend
{
public:
    esp_err_t load(void *data, size_t size) override;
    esp_err_t save(const void *data, size_t size) override;

private:
    esp_err_t init_nvs();
};
