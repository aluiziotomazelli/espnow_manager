#include "node_state_machine.hpp"
#include "esp_log.h"

static const char *TAG = "NodeStateMachine";

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
    return transition_to(has_peers ? NodeState::OPERATIONAL : NodeState::IDLE);
}

esp_err_t NodeStateMachine::on_deinit()
{
    state_.store(NodeState::UNINITIALIZED);
    return ESP_OK;
}

esp_err_t NodeStateMachine::on_pairing_requested()
{
    NodeState current = state_.load();
    if (current != NodeState::IDLE && current != NodeState::OPERATIONAL) {
        return ESP_ERR_INVALID_STATE;
    }
    return transition_to(NodeState::PAIRING);
}

esp_err_t NodeStateMachine::on_pairing_completed(bool success)
{
    if (state_.load() != NodeState::PAIRING) {
        return ESP_ERR_INVALID_STATE;
    }
    return transition_to(success ? NodeState::OPERATIONAL : NodeState::IDLE);
}

esp_err_t NodeStateMachine::on_scan_requested()
{
    NodeState current = state_.load();
    if (current != NodeState::OPERATIONAL && current != NodeState::PAIRING) {
        return ESP_ERR_INVALID_STATE;
    }
    return transition_to(NodeState::SCANNING);
}

esp_err_t NodeStateMachine::on_channel_found(bool is_hub, bool has_peers)
{
    NodeState current = state_.load();
    if (current != NodeState::SCANNING && current != NodeState::PAIRING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (is_hub) {
        // Current HUBs don't scan, but this supports future "Bridge" types 
        // or mobile HUBs that might need to rediscover a channel.
        return transition_to(NodeState::OPERATIONAL);
    }

    if (has_peers) {
        return transition_to(NodeState::OPERATIONAL);
    }
    else {
        return transition_to(NodeState::PAIRING);
    }
}

esp_err_t NodeStateMachine::on_scan_failed(bool is_pairing_active, bool has_peers)
{
    NodeState current = state_.load();

    if (current == NodeState::PAIRING) {
        if (is_pairing_active) {
            return transition_to(NodeState::SCANNING);
        }
        else {
            return transition_to(NodeState::IDLE);
        }
    }
    else if (current == NodeState::SCANNING) {
        if (is_pairing_active) {
            // Stay in SCANNING to allow for automatic retry loops during pairing
            return transition_to(NodeState::SCANNING);
        }
        else if (has_peers) {
            return transition_to(NodeState::OPERATIONAL);
        }
        else {
            return transition_to(NodeState::IDLE);
        }
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t NodeStateMachine::transition_to(NodeState new_state)
{
    ESP_LOGI(TAG, "NodeState: %d -> %d", static_cast<int>(state_.load()), static_cast<int>(new_state));
    state_.store(new_state);
    return ESP_OK;
}
