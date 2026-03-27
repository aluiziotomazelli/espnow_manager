#include "node_state_machine.hpp"
#include "esp_log.h"

/**
 * State Transition Table
 * ----------------------
 * | Current State   | Event                 | New State     | Condition / Rationale                        |
 * | :-------------- | :-------------------- | :------------ | :------------------------------------------- |
 * | UNINITIALIZED   | on_init               | OPERATIONAL   | has_peers == true                            |
 * | UNINITIALIZED   | on_init               | PAIRING_SCAN  | has_peers == false -> Auto-start pairing     |
 * | IDLE            | on_pairing_requested  | PAIRING_SCAN  | has_peers == false -> Need to find HUB       |
 * | IDLE            | on_pairing_requested  | PAIRING       | has_peers == true -> HUB already on channel  |
 * | OPERATIONAL     | on_pairing_requested  | PAIRING       | has_peers == true -> Already on channel      |
 * | OPERATIONAL     | on_pairing_requested  | PAIRING_SCAN  | has_peers == false -> No peers, need scan    |
 * | OPERATIONAL     | on_scan_requested     | RECOVERY_SCAN | Link lost, try to find channel               |
 * | PAIRING_SCAN    | on_channel_found      | PAIRING       | Channel found, ready to pair                 |
 * | PAIRING_SCAN    | on_scan_failed        | IDLE          | No HUB found. App can retry.                 |
 * | RECOVERY_SCAN   | on_channel_found      | OPERATIONAL   | Back to normal                               |
 * | RECOVERY_SCAN   | on_scan_failed        | IDLE          | Channel lost and not rediscovered.           |
 * | PAIRING         | on_pairing_timeout    | OPERATIONAL   | success == true                              |
 * | PAIRING         | on_pairing_timeout    | IDLE          | success == false && has_peers == false       |
 * | PAIRING         | on_pairing_timeout    | OPERATIONAL   | success == false && has_peers == true        |
 */

static const char* TAG = "NodeStateMachine";

NodeStateMachine::NodeStateMachine()
    : state_(NodeState::UNINITIALIZED)
{
}

NodeState NodeStateMachine::get_state() const
{
    return state_.load();
}

void NodeStateMachine::reset()
{
    state_.store(NodeState::UNINITIALIZED);
}

esp_err_t NodeStateMachine::on_init(bool has_peers)
{
    if (state_.load() != NodeState::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    return transition_to(has_peers ? NodeState::OPERATIONAL : NodeState::PAIRING_SCAN);
}

esp_err_t NodeStateMachine::on_deinit()
{
    state_.store(NodeState::UNINITIALIZED);
    return ESP_OK;
}

esp_err_t NodeStateMachine::on_pairing_requested(bool has_peers)
{
    NodeState current = state_.load();
    if (current != NodeState::IDLE && current != NodeState::OPERATIONAL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (has_peers) {
        return transition_to(NodeState::PAIRING);
    }
    else {
        return transition_to(NodeState::PAIRING_SCAN);
    }
}

esp_err_t NodeStateMachine::on_pairing_timeout(bool success, bool has_peers)
{
    if (state_.load() != NodeState::PAIRING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (success) {
        return transition_to(NodeState::OPERATIONAL);
    }
    else {
        return transition_to(has_peers ? NodeState::OPERATIONAL : NodeState::IDLE);
    }
}

esp_err_t NodeStateMachine::on_scan_requested()
{
    NodeState current = state_.load();
    if (current != NodeState::OPERATIONAL && current != NodeState::PAIRING && current != NodeState::IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    return transition_to(NodeState::RECOVERY_SCAN);
}

esp_err_t NodeStateMachine::on_channel_found()
{
    NodeState current = state_.load();
    if (current == NodeState::PAIRING_SCAN) {
        return transition_to(NodeState::PAIRING);
    }
    else if (current == NodeState::RECOVERY_SCAN) {
        return transition_to(NodeState::OPERATIONAL);
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t NodeStateMachine::on_scan_failed(bool has_peers)
{
    NodeState current = state_.load();

    if (current == NodeState::PAIRING_SCAN || current == NodeState::RECOVERY_SCAN) {
        return transition_to(NodeState::IDLE);
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t NodeStateMachine::transition_to(NodeState new_state)
{
    ESP_LOGI(TAG, "NodeState: %d -> %d", static_cast<int>(state_.load()), static_cast<int>(new_state));
    state_.store(new_state);
    return ESP_OK;
}
