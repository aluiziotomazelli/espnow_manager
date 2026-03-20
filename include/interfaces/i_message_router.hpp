// include/internface/i_message_router.hpp
#pragma once

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
    virtual void handle_packet(const DecodedPacket &decoded) = 0;
};