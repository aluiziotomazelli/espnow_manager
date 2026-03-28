// include/discovery_manager.hpp
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i_discovery_manager.hpp"
#include "i_hal_wifi.hpp"
#include "i_hal_freertos.hpp"
#include "i_message_codec.hpp"
#include "protocol_types.hpp"

class DiscoveryManager : public IDiscoveryManager
{
public:
    DiscoveryManager(IWiFiHAL& wifi_hal, IMessageCodec& message_codec, IFreeRTOSHAL& freertos_hal);

    /** @copydoc IDiscoveryManager::init */
    esp_err_t
    init(NodeId id, NodeType type, TaskHandle_t rx_task_handle, UBaseType_t priority, uint32_t stack_size) override;

    /** @copydoc IDiscoveryManager::deinit */
    void deinit() override;

    /** @copydoc IDiscoveryManager::start_scan */
    void start_scan() override;

    /** @copydoc IDiscoveryManager::stop_scan */
    void stop_scan() override;

    /** @copydoc IDiscoveryManager::is_scanning */
    bool is_scanning() const override { return is_scanning_.load(); };

    /** @copydoc IDiscoveryManager::handle_scan_probe */
    void handle_scan_probe(const DecodedPacket& decoded) override;

    /** @copydoc IDiscoveryManager::set_channel */
    void set_channel(uint8_t channel) override;

    /** @copydoc IDiscoveryManager::get_channel */
    uint8_t get_channel() const override { return current_channel_.load(); };

protected:
    // Scan Probe helpers
    esp_err_t send_scan_probe();
    bool hub_was_found();
    bool should_stop_scan();
    MessageHeader make_probe_header();
    esp_err_t scan_channel();

    // Scan Response helpers
    esp_err_t send_scan_response();
    MessageHeader make_response_header();
    TaskHandle_t get_task_handle() { return discovery_task_handle_; }

private:
    // Dependencies
    IWiFiHAL& hal_wifi_;
    IMessageCodec& message_codec_;
    IFreeRTOSHAL& hal_freertos_;

    // Task related
    TaskHandle_t discovery_task_handle_ = nullptr;
    static void discovery_task_func(void* arg);
    void discovery_task();

    TaskHandle_t rx_task_handle_ = nullptr;

    NodeId destination_node_id_;

    void notify_rx_task(uint32_t notification);

    // State
    bool hub_ready_ = false;
    bool node_ready_ = false;
    std::atomic<bool> is_scanning_ = false;

    std::atomic<uint8_t> current_channel_ = 1;

    // Node info
    NodeId my_node_id_;
    NodeType my_node_type_;
};
