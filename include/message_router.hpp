// include/message_router.hpp
#pragma once

#include "i_discovery_manager.hpp"
#include "i_heartbeat_manager.hpp"
#include "i_message_codec.hpp"
#include "i_message_router.hpp"
#include "i_pairing_manager.hpp"
#include "i_tx_manager.hpp"

class MessageRouter : public IMessageRouter
{
public:
    MessageRouter(
        IDiscoveryManager &discovery_manager,
        ITxManager &tx_manager,
        IHeartbeatManager &heartbeat_manager,
        IPairingManager &pairing_manager,
        IMessageCodec &message_codec);

    void set_app_queue(QueueHandle_t app_queue) override { app_queue_ = app_queue; }

    using IMessageRouter::set_node_info;
    void set_node_info(NodeId id, NodeType type) override
    {
        my_id_ = id;
        my_type_ = type;
    }

    void handle_packet(const RxPacket &packet) override;
    bool should_dispatch_to_worker(MessageType type) override;

private:
    IDiscoveryManager &discovery_manager_;
    ITxManager &tx_manager_;
    IHeartbeatManager &heartbeat_manager_;
    IPairingManager &pairing_manager_;
    IMessageCodec &message_codec_;

    QueueHandle_t app_queue_ = nullptr;
    NodeId my_id_ = ReservedIds::HUB;
    NodeType my_type_ = ReservedTypes::HUB;
};
