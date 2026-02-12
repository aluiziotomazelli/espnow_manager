# Host Tests

This directory contains Linux-based unit tests for the EspNow component.
Tests are organized by class/responsibility.

## Structure
- `espnow_facade/`: Tests for the main `EspNow` class using dependency injection and mocks.
- `peer_manager/`: (Planned) Tests for the `PeerManager` class.
- `tx_state_machine/`: (Planned) Tests for the `TxStateMachine` class.

## How to run
(Specific instructions depend on the environment setup, typically using `idf.py build` or `cmake` for host target)
