#pragma once

#include <atomic>

#include "i_node_state_machine.hpp"

/**
 * @brief Concrete implementation of the Node State Machine.
 */
class NodeStateMachine : public INodeStateMachine
{
public:
    NodeStateMachine();
    virtual ~NodeStateMachine() = default;

    NodeState get_state() const override;
    void reset() override;

    esp_err_t on_init(bool has_peers) override;
    esp_err_t on_deinit() override;
    esp_err_t on_pairing_requested() override;
    esp_err_t on_pairing_timeout(bool success) override;
    esp_err_t on_scan_requested() override;
    esp_err_t on_channel_found(bool is_hub, bool has_peers) override;
    esp_err_t on_scan_failed(bool is_pairing_active, bool has_peers) override;

private:
    std::atomic<NodeState> state_;
    esp_err_t transition_to(NodeState new_state);
};
