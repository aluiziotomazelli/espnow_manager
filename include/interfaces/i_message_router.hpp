// include/internface/i_message_router.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "espnow_types.hpp"

/**
 * @interface IMessageRouter
 * @brief Routing of received packets to appropriate managers or app queue
 * (internal)
 * @internal
 */
class IMessageRouter
{
public:
    virtual ~IMessageRouter() = default;

    /** @internal */
    virtual void handle_packet(const RxPacket &packet) = 0;
};