#pragma once

#include "espnow_interfaces.hpp"

class RealChannelScanner : public IChannelScanner
{
public:
    RealChannelScanner(IWiFiHAL &wifi_hal, IMessageCodec &message_codec, NodeId my_node_id, NodeType my_node_type);

    using IChannelScanner::update_node_info;

    ScanResult scan(uint8_t start_channel) override;
    void update_node_info(NodeId id, NodeType type) override;

private:
    IWiFiHAL &wifi_hal_;
    IMessageCodec &message_codec_;
    NodeId my_node_id_;
    NodeType my_node_type_;
};
