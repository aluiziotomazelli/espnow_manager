#include "node_state_machine.hpp"
#include "esp_log.h"

/**
 * State Transition Table
 * ----------------------
 * | Current State   | Event                      | New State     | Condition / Rationale | | :-------------- |
 * :------------------------- | :------------ | :-------------------------------------------------------- | |
 * UNINITIALIZED   | on_init(is_hub, has_peers) | OPERATIONAL   | has_peers == true | | UNINITIALIZED   |
 * on_init(is_hub, has_peers) | PAIRING       | has_peers == false && is_hub == true                      | |
 * UNINITIALIZED   | on_init(is_hub, has_peers) | PAIRING_SCAN  | has_peers == false && is_hub == false | | IDLE |
 * on_pairing_requested       | PAIRING       | is_hub == true || has_peers == true                       | | IDLE |
 * on_pairing_requested       | PAIRING_SCAN  | is_hub == false && has_peers == false                     | |
 * OPERATIONAL     | on_pairing_requested       | PAIRING       | is_hub == true || has_peers == true | | OPERATIONAL |
 * on_pairing_requested       | PAIRING_SCAN  | is_hub == false && has_peers == false                     | |
 * OPERATIONAL     | on_scan_requested          | RECOVERY_SCAN | Link lost, try to find channel | | PAIRING         |
 * on_scan_requested          | RECOVERY_SCAN | Scan requested during pairing                             | | IDLE |
 * on_scan_requested          | RECOVERY_SCAN | Scan requested from idle                                  | |
 * PAIRING_SCAN    | on_channel_found           | PAIRING       | Channel found, ready to pair | | RECOVERY_SCAN   |
 * on_channel_found           | OPERATIONAL   | Back to normal                                            | |
 * PAIRING_SCAN    | on_scan_failed(has_peers)  | IDLE          | No HUB found. App can retry. | | RECOVERY_SCAN   |
 * on_scan_failed(has_peers)  | IDLE          | Channel lost and not rediscovered.                        | | PAIRING |
 * on_pairing_timeout         | OPERATIONAL   | has_peers == true                                         | | PAIRING |
 * on_pairing_timeout         | IDLE          | has_peers == false                                        |
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

esp_err_t NodeStateMachine::on_init(bool is_hub, bool has_peers)
{
    if (state_.load() != NodeState::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (has_peers) {
        return transition_to(NodeState::OPERATIONAL);
    }
    // HUB without peers goes directly to PAIRING — already on the correct channel
    // NODE without peers needs to find the HUB channel first
    return transition_to(is_hub ? NodeState::PAIRING : NodeState::PAIRING_SCAN);
}

esp_err_t NodeStateMachine::on_deinit()
{
    // state_.store(NodeState::UNINITIALIZED);
    return transition_to(NodeState::UNINITIALIZED);
}

esp_err_t NodeStateMachine::on_pairing_requested(bool is_hub, bool has_peers)
{
    NodeState current = state_.load();
    if (current != NodeState::IDLE && current != NodeState::OPERATIONAL) {
        return ESP_ERR_INVALID_STATE;
    }
    // HUB never scans — already on the correct channel
    // NODE without peers needs to find the HUB channel first
    if (is_hub || has_peers) {
        return transition_to(NodeState::PAIRING);
    }
    return transition_to(NodeState::PAIRING_SCAN);
}

esp_err_t NodeStateMachine::on_pairing_timeout(bool has_peers)
{
    if (state_.load() != NodeState::PAIRING) {
        return ESP_ERR_INVALID_STATE;
    }

    return transition_to(has_peers ? NodeState::OPERATIONAL : NodeState::IDLE);
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

esp_err_t NodeStateMachine::on_scan_failed()
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
