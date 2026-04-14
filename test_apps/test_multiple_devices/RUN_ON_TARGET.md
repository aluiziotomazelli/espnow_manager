# Running On-Target Tests — Step-by-Step Guide

This guide explains how to run the multi-device ESP-NOW tests using **two physical ESP32 devices** connected via USB to the same host machine.

---

## Prerequisites

- **2x ESP32 devices** (any variant: ESP32, ESP32-C3, ESP32-S3, etc.)
- **ESP-IDF v5.1.1+** installed and exported
- **USB cables** connecting both devices to the host
- **Two terminal windows** (one per device)

---

## 1. Identify Serial Ports

Find which `/dev/ttyUSB*` or `/dev/ttyACM*` ports your devices are on:

```bash
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
# Example output:
# /dev/ttyUSB0  → DUT1 (HUB)
# /dev/ttyUSB1  → DUT2 (NODE)
```

**Label them:**
- **DUT1 (Device Under Test 1)** = **HUB** — will run terminal 1
- **DUT2 (Device Under Test 2)** = **NODE** — will run terminal 2

---

## 2. Build and Flash Both Devices

Open **two terminals** side by side.

### Terminal 1 — HUB (DUT1)

```bash
cd /home/german/dev/workspaces/idf-components/espnow_manager/test_apps/test_multiple_hub
. ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyUSB0 flash monitor
```

### Terminal 2 — NODE (DUT2)

```bash
cd /home/german/dev/workspaces/idf-components/espnow_manager/test_apps/test_multiple_node
. ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyUSB1 flash monitor
```

> **Note:** Adjust `/dev/ttyUSB0` and `/dev/ttyUSB1` to match your actual ports.

---

## 3. Exiting the Serial Monitor Properly

To close the monitor and **free the serial port** for other applications (or for the next test run), you must use the escape sequence:

- Press **`Ctrl + ]`**

> [!IMPORTANT]
> If you close the terminal window without pressing `Ctrl + ]`, the `idf_monitor` process may remain running in the background, locking the serial port. If this happens, you will need to find and kill the process manually:
> ```bash
> lsof /dev/ttyUSB0  # Find PID
> kill -9 <PID>
> ```

---

## 4. Understanding the Test Menu

After flashing, each device boots into the **Unity test menu**. You'll see something like:

```
Press ENTER to see the list of tests.
```

**This is the key rule:**
- On **Terminal 1 (HUB)**, type `1` + **Enter** to select test 1, `2` + Enter for test 2, etc.
- On **Terminal 2 (NODE)**, type the **same test number** + Enter.
- **Both terminals must be given the same test number** — they are two halves of the same test.

---

## 5. Signal Coordination — The Most Important Part

Tests use `unity_send_signal()` and `unity_wait_for_signal()` for synchronization between devices.

### How it works:

1. **Device A** calls `unity_send_signal("some signal name")`:
   - Prints `Send signal: some signal!` to its UART console
   - **Continues execution immediately** (does not block)

2. **Device B** calls `unity_wait_for_signal("some signal name")`:
   - Prints `Waiting for signal: some signal! Please press "Enter" key to once any board send this signal.`
   - **Blocks execution** until you press **Enter**

3. **Your role as operator:**
   - When you see `Send signal: xxx!` on **Terminal 1**, switch to **Terminal 2** and press **Enter**
   - When you see `Send signal: xxx!` on **Terminal 2**, switch to **Terminal 1** and press **Enter**

### Visual example:

```
Terminal 1 (HUB)                          Terminal 2 (NODE)
─────────────────                          ──────────────────
Running test...                            
                                          unity_wait_for_signal("hub ready")
                                          → "Waiting for signal: hub ready!"
                                          → **BLOCKED, waiting for Enter**

unity_send_signal("hub ready")            
→ "Send signal: hub ready!"               
→ **CONTINUES execution**                 
                                          
                                          ← You see the signal message on Terminal 1
                                          ← You switch to Terminal 2 and press Enter
                                          ← Terminal 2 unblocks, continues execution
```

**Rule of thumb:** The device that **sends** the signal keeps going. The device that **waits** needs you to press Enter on its terminal after you see the signal message on the other terminal.

---

## 6. Running Test 19 — EdgeCaseMalformedPacketsIgnored

This is the newest test (added to verify malformed packet handling).

### Step-by-Step:

| Step | Terminal 1 (HUB) | Terminal 2 (NODE) | Action |
|------|-----------------|-------------------|--------|
| 1 | Type `19` + Enter | Type `19` + Enter | Start test on both |
| 2 | Shows: `Waiting for signal: node ready for malformed!` | Auto-pairs, then shows: `Send signal: node ready for malformed!` | Press **Enter** on Terminal 1 |
| 3 | Waits 500ms, checks queue (should be empty), shows: `Send signal: hub received nothing from malformed!` | Shows: `Waiting for signal: hub received nothing from malformed!` | Press **Enter** on Terminal 2 |
| 4 | Waits for valid packet, receives it, shows: `Send signal: hub received valid after malformed!` | Sends valid packet, shows: `Send signal: node sent valid after malformed!` | — |
| 5 | Shows: `Waiting for signal: hub received valid after malformed!` | Shows: `Waiting for signal: hub received valid after malformed!` | Press **Enter** on Terminal 2 |
| 6 | ✅ Test passes | ✅ Test passes | Both show `TEST PASSED` |

### What to expect on each terminal:

**Terminal 1 (HUB):**
```
Running 19. Integration: EdgeCaseMalformedPacketsIgnored...
Waiting for signal: node ready for malformed! Please press "Enter"...
  ← Press Enter after you see "node ready for malformed" on Terminal 2
Send signal: hub received nothing from malformed!
  ← (no packet in queue, as expected — malformed packet was dropped by CRC check)
Send signal: hub received valid after malformed!
  ← (valid packet arrived from NODE)
TEST PASSED
```

**Terminal 2 (NODE):**
```
Running 19. Integration: EdgeCaseMalformedPacketsIgnored...
Send signal: node ready for malformed!
  ← (NODE is paired and ready to send malformed packet)
Waiting for signal: hub received nothing from malformed! Please press "Enter"...
  ← Press Enter after you see the signal on Terminal 1
Send signal: node sent valid after malformed!
  ← (valid packet sent via TxManager)
Waiting for signal: hub received valid after malformed! Please press "Enter"...
  ← Press Enter after you see the signal on Terminal 1
TEST PASSED
```

---

## 7. Running All Tests (Quick Reference)

| Test # | Name | What to do |
|--------|------|-----------|
| 1 | Setup: Clear NVS | Run **ONLY ONCE** before starting your test session. Not needed between tests as `tearDown()` handles cleanup automatically. |
| 2 | HubAndNodePairSuccessfully | Auto-pairing test. Type `2` + Enter on both. Follow signal prompts. |
| 3 | NodeSendsDataHubReceives | Simple data transfer (no ACK). |
| 4 | NodeSendsDataHubReceivesAndAcks | Data + ACK flow. |
| 5 | AckTimeoutRetriesAndSuccess | HUB delays ACK, forces NODE retry. |
| 6 | HeartbeatSentPeriodically | Verifies periodic heartbeats. |
| 7 | PeersPersistedToNvsAndRestored | Two-cycle persistence test. |
| 8 | DiscoveryScanFindsHubOnDifferentChannel | Cross-channel discovery. |
| 9 | HubChangesChannelNodesRecover | Channel change + recovery scan. |
| 10 | ScanFailsWhenNoHubPresent | HUB "dies", NODE scans and goes to IDLE. |
| 11 | HeartbeatTimeoutMarksPeerOffline | NODE stops, HUB marks offline. |
| 12 | HubUpdatesNodeIdOnMacCollision | MAC collision handling. |
| 13 | HeartbeatResetsOfflineTimer | Reboot without re-pairing. |
| 14 | NvsBackupUsedWhenRtcCorrupt | RTC corruption fallback. |
| 15 | NodeWakesFromDeepSleepWithPeersIntact | Deep sleep + NVS persistence. |
| 16 | RtcStorageSurvivesDeepSleep | Deep sleep + RTC persistence. |
| 17 | DataStressTest | 100 packets burst. |
| 18 | StressTestWithAckAndDeduplication | ACK stress + dedup. |
| **19** | **EdgeCaseMalformedPacketsIgnored** | **Malformed packet dropped by CRC** |

---

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `ESP_ERR_NOT_FOUND` on peer MAC | Devices not paired yet | Increase `kWaitAfterPairingMs` or check WiFi channel |
| Test hangs on `Waiting for signal` | You missed pressing Enter on the other terminal | Ctrl+C both, restart the test |
| `ESP_ERR_TIMEOUT` on mutex | NVS flash operation too slow | Normal — the 5ms timeout on TX path handles this |
| Both devices show `PAIRING_SCAN` forever | Different WiFi channels or too far apart | Ensure both use channel 1 (or let scan find the HUB) |
| `unity_send_signal` message not visible on other terminal | Expected — signal is just a prompt, not a UART message | Press Enter manually on the waiting terminal |
| Device crashes after deinit | RTC storage not cleared properly | `clear_rtc_storage()` is called in `tearDown()` |

---

## 9. Expected Results

When all tests pass, each terminal should show:

```
TEST PASSED
All tests passed
```

If a test fails, you'll see:

```
TEST FAILED
Expression: <condition> failed
```

Re-run the failing test individually by typing its number again.

---

*Last updated: 2026-04-13 (Updated Test List & Exit Instructions)*
