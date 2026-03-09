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

    /** @internal */
    virtual bool should_dispatch_to_worker(MessageType type) = 0;

    /** @internal */
    virtual void set_app_queue(QueueHandle_t app_queue) = 0;

    /** @internal */
    virtual void set_node_info(NodeId id, NodeType type) = 0;

    /** @internal */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>

    void set_node_info(T1 id, T2 type)
    {
        set_node_info(static_cast<NodeId>(id), static_cast<NodeType>(type));
    }
};