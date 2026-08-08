#pragma once

#include <atomic>

#include "i_node_state_machine.hpp"
namespace espnow {

/**
 * @brief Concrete implementation of the Node State Machine.
 */
class NodeStateMachine : public INodeStateMachine
{
public:
    NodeStateMachine();
    virtual ~NodeStateMachine() = default;

    /** @copydoc INodeStateMachine::get_state */
    NodeState get_state() const override;

    /** @copydoc INodeStateMachine::reset */
    void reset() override;

    /** @copydoc INodeStateMachine::on_init */
    esp_err_t on_init(bool is_hub, bool has_peers) override;

    /** @copydoc INodeStateMachine::on_deinit */
    esp_err_t on_deinit() override;

    /** @copydoc INodeStateMachine::on_pairing_requested */
    esp_err_t on_pairing_requested(bool is_hub, bool has_peers) override;

    /** @copydoc INodeStateMachine::on_pairing_timeout */
    esp_err_t on_pairing_timeout(bool has_peers) override;

    /** @copydoc INodeStateMachine::on_scan_requested */
    esp_err_t on_scan_requested(bool is_hub) override;

    /** @copydoc INodeStateMachine::on_channel_found */
    esp_err_t on_channel_found() override;

    /** @copydoc INodeStateMachine::on_scan_failed */
    esp_err_t on_scan_failed() override;

private:
    std::atomic<NodeState> state_;
    esp_err_t transition_to(NodeState new_state);
};

} // namespace espnow
