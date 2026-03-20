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

    void handle_packet(const RxPacket &packet) override;

private:
    IDiscoveryManager &discovery_manager_;
    ITxManager &tx_manager_;
    IHeartbeatManager &heartbeat_manager_;
    IPairingManager &pairing_manager_;
    IMessageCodec &message_codec_;
};
