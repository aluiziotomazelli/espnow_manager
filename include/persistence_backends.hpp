#pragma once

#include "i_persistence_backend.hpp"
#include "storage_manager.hpp"

/**
 * @brief Default RTC backend that uses a static PersistentData variable.
 * On real hardware, this variable is placed in RTC slow memory.
 */
class RtcBackend : public IPersistenceBackend
{
public:
    RtcBackend(PersistentData *storage_ptr = nullptr);

    esp_err_t load(void *data, size_t size) override;
    esp_err_t save(const void *data, size_t size) override;

private:
    PersistentData *storage_;
};

/**
 * @brief Default NVS backend that uses the nvs_flash component.
 */
class NvsBackend : public IPersistenceBackend
{
public:
    esp_err_t load(void *data, size_t size) override;
    esp_err_t save(const void *data, size_t size) override;

private:
    esp_err_t init_nvs();
};
