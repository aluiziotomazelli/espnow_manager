// include/interfaces/i_bootstraper.hpp
#pragma once

#include "esp_err.h"

/**
 * @interface IBootstraper
 * @brief Bootstraper interface (internal)
 * @internal
 */
class IBootstraper
{
public:
    virtual ~IBootstraper() = default;

    virtual esp_err_t init() = 0;

    virtual esp_err_t deinit() = 0;
};