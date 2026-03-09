// include/internface/i_persistence_backend.hpp
#pragma once

#include <cstdint>
#include <vector>

#include "esp_err.h"

/**
 * @interface IPersistenceBackend
 * @brief Low-level storage backend (NVS/RTC) (internal)
 * @internal
 */
class IPersistenceBackend
{
public:
    virtual ~IPersistenceBackend() = default;

    /** @internal */
    virtual esp_err_t load(void *data, size_t size) = 0;

    /** @internal */
    virtual esp_err_t save(const void *data, size_t size) = 0;
};