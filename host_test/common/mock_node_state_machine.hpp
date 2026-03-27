#pragma once

#include "gmock/gmock.h"
#include "i_node_state_machine.hpp"

class MockNodeStateMachine : public INodeStateMachine
{
public:
    MockNodeStateMachine()
    {
        using ::testing::_;
        using ::testing::Invoke;
        using ::testing::Return;

        ON_CALL(*this, get_state()).WillByDefault(Invoke([this]() { return state_; }));

        ON_CALL(*this, on_init(_)).WillByDefault(Invoke([this](bool has_peers) {
            state_ = has_peers ? NodeState::OPERATIONAL : NodeState::IDLE;
            return ESP_OK;
        }));

        ON_CALL(*this, on_deinit()).WillByDefault(Invoke([this]() {
            state_ = NodeState::UNINITIALIZED;
            return ESP_OK;
        }));

        ON_CALL(*this, on_pairing_requested(_)).WillByDefault(Invoke([this](bool has_peers) {
            state_ = NodeState::PAIRING;
            return ESP_OK;
        }));

        ON_CALL(*this, on_scan_requested()).WillByDefault(Invoke([this]() {
            state_ = NodeState::SCANNING;
            return ESP_OK;
        }));

        ON_CALL(*this, on_pairing_timeout(_, _)).WillByDefault(Invoke([this](bool success, bool has_peers) {
            state_ = has_peers ? NodeState::OPERATIONAL : NodeState::IDLE;
            return ESP_OK;
        }));

        ON_CALL(*this, on_channel_found()).WillByDefault(Invoke([this]() {
            state_ = NodeState::OPERATIONAL;
            return ESP_OK;
        }));

        ON_CALL(*this, on_scan_failed(_)).WillByDefault(Invoke([this](bool has_peers) {
            state_ = has_peers ? NodeState::OPERATIONAL : NodeState::IDLE;
            return ESP_OK;
        }));
    }

    MOCK_METHOD(NodeState, get_state, (), (const, override));
    MOCK_METHOD(void, reset, (), (override));
    MOCK_METHOD(esp_err_t, on_init, (bool has_peers), (override));
    MOCK_METHOD(esp_err_t, on_deinit, (), (override));
    MOCK_METHOD(esp_err_t, on_pairing_requested, (bool has_peers), (override));
    MOCK_METHOD(esp_err_t, on_pairing_timeout, (bool success, bool has_peers), (override));
    MOCK_METHOD(esp_err_t, on_scan_requested, (), (override));
    MOCK_METHOD(esp_err_t, on_channel_found, (), (override));
    MOCK_METHOD(esp_err_t, on_scan_failed, (bool has_peers), (override));

    void set_state(NodeState s) { state_ = s; }

private:
    NodeState state_ = NodeState::UNINITIALIZED;
};
