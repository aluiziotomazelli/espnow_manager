#pragma once

#include "gmock/gmock.h"
#include "i_node_state_machine.hpp"

class MockNodeStateMachine : public INodeStateMachine
{
public:
    void set_state(NodeState s) { state_ = s; }

    MockNodeStateMachine()
    {
        using ::testing::_;
        using ::testing::Invoke;
        using ::testing::ReturnPointee;

        ON_CALL(*this, get_state()).WillByDefault(ReturnPointee(&state_));

        ON_CALL(*this, reset()).WillByDefault(Invoke([this]() { state_ = NodeState::UNINITIALIZED; }));

        ON_CALL(*this, on_init(_)).WillByDefault(Invoke([this](bool has_peers) {
            state_ = has_peers ? NodeState::OPERATIONAL : NodeState::PAIRING_SCAN;
            return ESP_OK;
        }));
        ON_CALL(*this, on_deinit()).WillByDefault(Invoke([this]() {
            state_ = NodeState::UNINITIALIZED;
            return ESP_OK;
        }));
        ON_CALL(*this, on_pairing_requested(_)).WillByDefault(Invoke([this](bool has_peers) {
            state_ = has_peers ? NodeState::PAIRING : NodeState::PAIRING_SCAN;
            return ESP_OK;
        }));
        ON_CALL(*this, on_scan_requested()).WillByDefault(Invoke([this]() {
            state_ = NodeState::RECOVERY_SCAN;
            return ESP_OK;
        }));
        ON_CALL(*this, on_channel_found()).WillByDefault(Invoke([this]() {
            state_ = (state_ == NodeState::RECOVERY_SCAN) ? NodeState::OPERATIONAL : NodeState::PAIRING;
            return ESP_OK;
        }));
        ON_CALL(*this, on_scan_failed(_)).WillByDefault(Invoke([this](bool has_peers) {
            state_ = NodeState::IDLE;
            return ESP_OK;
        }));
        ON_CALL(*this, on_pairing_timeout(_)).WillByDefault(Invoke([this](bool has_peers) {
            state_ = has_peers ? NodeState::OPERATIONAL : NodeState::IDLE;
            return ESP_OK;
        }));
    }

    MOCK_METHOD(NodeState, get_state, (), (const, override));
    MOCK_METHOD(void, reset, (), (override));
    MOCK_METHOD(esp_err_t, on_init, (bool has_peers), (override));
    MOCK_METHOD(esp_err_t, on_deinit, (), (override));
    MOCK_METHOD(esp_err_t, on_pairing_requested, (bool has_peers), (override));
    MOCK_METHOD(esp_err_t, on_scan_requested, (), (override));
    MOCK_METHOD(esp_err_t, on_channel_found, (), (override));
    MOCK_METHOD(esp_err_t, on_scan_failed, (bool has_peers), (override));
    MOCK_METHOD(esp_err_t, on_pairing_timeout, (bool has_peers), (override));

private:
    NodeState state_ = NodeState::UNINITIALIZED;
};