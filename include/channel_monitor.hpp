// include/channel_monitor.hpp
#pragma once

#include "i_channel_monitor.hpp"
#include "i_hal_wifi.hpp"

class ChannelMonitor : public IChannelMonitor
{
public:
    ChannelMonitor(IWiFiHAL &hal_wifi);
    ~ChannelMonitor();

    esp_err_t init(IChannelObserver *observer, uint32_t interval_ms) override;

    void tick(uint64_t now_ms) override;

private:
    IWiFiHAL &hal_wifi_;
    IChannelObserver *observer_ = nullptr;
    bool is_active_ = false;

    uint32_t interval_ms_;
    uint64_t last_check_ms_ = 0;
    uint8_t last_known_channel_ = 0;

    void notify_channel_changed(uint8_t new_channel);
    uint8_t get_wifi_channel();
};