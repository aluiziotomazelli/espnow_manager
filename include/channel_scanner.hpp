#pragma once

#include "i_channel_scanner.hpp"
#include "i_hal_wifi.hpp"
#include "i_freertos_hal.hpp"
#include "i_message_codec.hpp"

class ChannelScanner : public IChannelScanner
{
public:
    ChannelScanner(
        IWiFiHAL &wifi_hal,
        IMessageCodec &message_codec,
        IFreeRTOSHAL &freertos_hal,
        NodeId my_node_id,
        NodeType my_node_type);

    using IChannelScanner::update_node_info;

    ScanResult scan(uint8_t start_channel) override;
    void update_node_info(NodeId id, NodeType type) override;

private:
    IWiFiHAL &wifi_hal_;
    IMessageCodec &message_codec_;
    IFreeRTOSHAL &freertos_hal_;

    NodeId my_node_id_;
    NodeType my_node_type_;
};
