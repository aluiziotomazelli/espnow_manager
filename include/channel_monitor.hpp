// include/channel_monitor.hpp
#pragma once

#include "i_channel_monitor.hpp"
#include "i_hal_wifi.hpp"
#include "i_channel_observer.hpp"

class ChanelMonitor : public IChannelMonitor
{
public:
    ChanelMonitor(IWiFiHAL &hal_wifi);
    ~ChanelMonitor() = default;

    void tick(uint64_t now_ms) override;

    uint8_t get_wifi_channel() const override;

private:
    IWiFiHAL &hal_wifi_;
    IChannelObserver *observer_ = nullptr;

    uint32_t interval_ms_;
    uint64_t last_tick_ms_;
    uint8_t last_know_channel_;
};