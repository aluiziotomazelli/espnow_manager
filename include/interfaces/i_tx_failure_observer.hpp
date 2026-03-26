// include/interfaces/i_tx_failure_observer.hpp
#pragma once

#include <cstdint>

/**
 * @interface ITxFailureObserver
 * @brief Observer interface for transmission failure notifications.
 *
 * Implemented by EspNowManager to receive notifications when TxManager
 * exhausts all retry attempts for a packet.
 */
class ITxFailureObserver
{
public:
    virtual ~ITxFailureObserver() = default;

    /**
     * @brief Called when MAX_FAILURES is reached for a packet.
     *
     * This is a notification only - the observer decides what action to take.
     * Typically triggers NodeState transition to SCANNING.
     *
     * @note Called from TxManager task context (thread-safe)
     */
    virtual void on_max_transmission_failures_cb() = 0;
};