#pragma once

#include "espnow_interfaces.hpp"
#include <queue>

class RealMessageRouter : public IMessageRouter
{
public:
    RealMessageRouter(IPeerManager &peer_manager,
                      ITxManager &tx_manager,
                      IHeartbeatManager &heartbeat_manager,
                      IPairingManager &pairing_manager,
                      IMessageCodec &message_codec);

    void set_app_queue(QueueHandle_t app_queue) override { app_queue_ = app_queue; }

    using IMessageRouter::set_node_info;
    void set_node_info(NodeId id, NodeType type) override
    {
        my_id_   = id;
        my_type_ = type;
    }

    void handle_packet(const RxPacket &packet) override;
    bool should_dispatch_to_worker(MessageType type) override;

private:
    void handle_scan_probe(const RxPacket &packet);

    IPeerManager &peer_manager_;
    ITxManager &tx_manager_;
    IHeartbeatManager &heartbeat_manager_;
    IPairingManager &pairing_manager_;
    IMessageCodec &message_codec_;

    QueueHandle_t app_queue_ = nullptr;
    NodeId my_id_ = ReservedIds::HUB;
    NodeType my_type_ = ReservedTypes::HUB;
};
