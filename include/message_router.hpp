// include/message_router.hpp
#pragma once

#include "i_discovery_manager.hpp"
#include "i_heartbeat_manager.hpp"
#include "i_pairing_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_message_router.hpp"

class MessageRouter : public IMessageRouter
{
public:
    MessageRouter(
        IDiscoveryManager &discovery_manager,
        ITxManager &tx_manager,
        IHeartbeatManager &heartbeat_manager,
        IPairingManager &pairing_manager);

    /** @copydoc IMessageRouter::handle_packet */
    void handle_packet(const DecodedPacket &decoded) override;

private:
    IDiscoveryManager &discovery_manager_;
    ITxManager &tx_manager_;
    IHeartbeatManager &heartbeat_manager_;
    IPairingManager &pairing_manager_;
};
