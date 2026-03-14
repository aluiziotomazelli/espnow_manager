// include/discovery_manager.hpp
#pragma once

#include "i_discovery_manager.hpp"
#include "i_hal_wifi.hpp"
#include "i_hal_freertos.hpp"
#include "i_channel_observer.hpp"
#include "i_message_codec.hpp"

class DiscoveryManager : public IDiscoveryManager
{
public:
    DiscoveryManager(IWiFiHAL &wifi_hal, IMessageCodec &message_codec, IFreeRTOSHAL &freertos_hal);

    esp_err_t init(NodeId id, NodeType type, ITxManager &tx_mgr, IChannelObserver *observer = nullptr) override;
    ScanResult scan(uint8_t start_channel) override;
    void handle_probe(const RxPacket &packet) override;

private:
    IWiFiHAL &hal_wifi_;
    IMessageCodec &message_codec_;
    IFreeRTOSHAL &hal_freertos_;
    ITxManager *tx_mgr_ = nullptr;
    IChannelObserver *observer_ = nullptr;

    NodeId my_node_id_ = ReservedIds::BROADCAST;
    NodeType my_node_type_ = ReservedTypes::UNKNOWN;
};
