
## Host Tests List and coverage
| TEST NAME                 | STATUS | Lines   | Functions | Branchs |
|---------------------------|--------|---------|-----------|---------|
| test_espnow_driver        |  OK    |  100%   |   100%    |  100%   |
| test_espnow_manager       |  OK    |         |           |         |
| test_discovery_manager    |  OK    |  97.6%  |   100%    |  98.2%  |
| test_heartbeat_manager    |  OK    |  100%   |   100%    |  100%   |
| test_message_codec        |  OK    |  100%   |   100%    |  100%   |
| test_message_router       |  OK    |  100%   |   100%    |  100%   |
| test_pairing_manager      |  OK    |  100%   |   100%    |  100%   |
| test_peer_manager         |  OK    |  100%   |   100%    |   84.3% |
| test_storage_manager      |  OK    |  100%   |   100%    |  100%   |
| test_tx_manager           |  OK    |   95.8% |   100%    |  90.2%  |
| test_tx_state_machine     |  OK    |  100%   |   100%    |  100%   |
| test_node_state_machine   |        |         |           |         |
| test_channel_monitor      |  OK    |  100%   |   100%    |  100%   |

## TODO:
 - Verify channel_monitor integration in espnow_manager
 - Verify node_state_machine integration with discovery
 - Verify new storage_manager integration - OK
 - Verify new tx_manager integration - OK
 - Verify new discovery_manager integration
 