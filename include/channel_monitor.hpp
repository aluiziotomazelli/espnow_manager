// include/channel_monitor.hpp
#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "i_channel_monitor.hpp"
#include "i_en_hal_freertos.hpp"
#include "i_en_hal_wifi.hpp"
namespace espnow {

class ChannelMonitor : public IChannelMonitor
{
public:
    ChannelMonitor(IWiFiHAL& hal_wifi, IFreeRTOSHAL& hal_freertos);
    ~ChannelMonitor();

    /** @copydoc IChannelMonitor::init */
    esp_err_t init(uint32_t interval_ms, TaskHandle_t rx_task_handle) override;

    /** @copydoc IChannelMonitor::deinit */
    void deinit() override;

    /** @copydoc IChannelMonitor::tick */
    void tick(int64_t now_ms) override;

    /** @copydoc IChannelMonitor::get_wifi_channel */
    uint8_t get_wifi_channel() override { return last_known_channel_.load(); };

private:
    IWiFiHAL& hal_wifi_;
    IFreeRTOSHAL& hal_freertos_;
    bool is_active_ = false;
    TaskHandle_t rx_task_handle_ = nullptr;

    uint32_t interval_ms_;
    int64_t last_check_ms_ = 0;
    std::atomic<uint8_t> last_known_channel_ = 0;

    uint8_t verify_wifi_channel();
    void notify_rx_task(uint32_t notifications);
};
} // namespace espnow
