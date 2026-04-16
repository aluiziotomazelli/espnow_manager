#!/usr/bin/env python3
"""
Run multi-device ESP-NOW tests on two physical ESP32 devices.

Usage:
    python3 run_multi_device_test.py <hub_port> <node_port> [test_number]

Example:
    python3 run_multi_device_test.py /dev/ttyUSB1 /dev/ttyUSB0 19
"""

import sys
import time
import serial
import threading
import queue

BAUDRATE = 115200
TIMEOUT = 0.1  # Serial read timeout (seconds)
SIGNAL_TIMEOUT = 30  # Max wait for a signal (seconds)


def flash_device(project_dir, port):
    """Flash a device without starting monitor."""
    import subprocess
    cmd = f'cd {project_dir} && . ~/esp/esp-idf/export.sh && idf.py -p {port} flash'
    print(f"  Flashing {port} from {project_dir}...")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        print(f"  Flash failed for {port}")
        print(result.stderr[-500:])
        return False
    print(f"  {port} flashed OK")
    return True


class DevicePort:
    def __init__(self, port, name):
        self.port = port
        self.name = name
        self.ser = None
        self.output_queue = queue.Queue()
        self.running = False
        self.thread = None

    def open(self):
        self.ser = serial.Serial(self.port, BAUDRATE, timeout=TIMEOUT)
        self.ser.reset_input_buffer()
        self.running = True
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()
        print(f"  {self.name} opened on {self.port}")

    def _read_loop(self):
        buf = ""
        while self.running:
            try:
                data = self.ser.read(256)
                if data:
                    text = data.decode("utf-8", errors="replace")
                    buf += text
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.rstrip("\r")
                        self.output_queue.put(line)
                        # Also print to console for debugging
                        print(f"[{self.name}] {line}")
            except Exception as e:
                if self.running:
                    print(f"[{self.name}] Read error: {e}")
                break

    def write(self, text):
        if self.ser and self.ser.is_open:
            self.ser.write(text.encode())
            print(f"  → [{self.name}] sent: {repr(text)}")

    def wait_for_line(self, substring, timeout_sec=SIGNAL_TIMEOUT):
        """Wait for a line containing the given substring."""
        deadline = time.time() + timeout_sec
        while time.time() < deadline:
            try:
                line = self.output_queue.get(timeout=0.5)
                if substring in line:
                    return line
            except queue.Empty:
                continue
        return None

    def close(self):
        self.running = False
        if self.ser:
            self.ser.close()


def run_test(hub_port, node_port, test_number):
    hub = DevicePort(hub_port, "HUB")
    node = DevicePort(node_port, "NODE")

    try:
        print("\n=== Opening serial ports ===")
        hub.open()
        node.open()

        # Give devices time to boot
        print("\n=== Waiting for devices to boot (5s) ===")
        time.sleep(5)

        # Clear any buffered output
        while not hub.output_queue.empty():
            hub.output_queue.get_nowait()
        while not node.output_queue.empty():
            node.output_queue.get_nowait()

        # Send test number to both devices
        print(f"\n=== Sending test number {test_number} to both devices ===")
        hub.write(f"{test_number}\n")
        node.write(f"{test_number}\n")

        # Now coordinate signals
        print("\n=== Coordinating test signals ===\n")

        # We need to watch both devices for:
        # - "Waiting for signal:" → this device needs Enter on the OTHER
        # - "Send signal:" → the OTHER device needs Enter

        test_running = True
        max_duration = 120  # Max test duration in seconds
        start_time = time.time()

        while test_running and (time.time() - start_time) < max_duration:
            # Check for "Waiting for signal" on HUB
            line = hub.wait_for_line("Waiting for signal:", timeout_sec=2)
            if line and test_running:
                print(f"  ⏳ HUB waiting for signal → sending Enter to NODE")
                node.write("\n")

            # Check for "Waiting for signal" on NODE
            line = node.wait_for_line("Waiting for signal:", timeout_sec=2)
            if line and test_running:
                print(f"  ⏳ NODE waiting for signal → sending Enter to HUB")
                hub.write("\n")

            # Check for test completion
            hub_line = hub.wait_for_line("TEST", timeout_sec=0.5)
            node_line = node.wait_for_line("TEST", timeout_sec=0.5)

            if hub_line:
                print(f"  📋 HUB: {hub_line}")
                if "PASSED" in hub_line or "FAILED" in hub_line:
                    print(f"\n{'='*60}")
                    print(f"  ✅ HUB test result: {hub_line}")
                    print(f"{'='*60}\n")

            if node_line:
                print(f"  📋 NODE: {node_line}")
                if "PASSED" in node_line or "FAILED" in node_line:
                    print(f"\n{'='*60}")
                    print(f"  ✅ NODE test result: {node_line}")
                    print(f"{'='*60}\n")

            # Check if both tests are done
            if "PASSED" in str(hub_line or "") and "PASSED" in str(node_line or ""):
                test_running = False

            # Also check output queue for completion messages
            if "All tests" in str(hub_line or "") or "All tests" in str(node_line or ""):
                test_running = False

        print("\n=== Test coordination complete ===")

    except KeyboardInterrupt:
        print("\n\n⚠️  Interrupted by user")
    except Exception as e:
        print(f"\n❌ Error: {e}")
    finally:
        hub.close()
        node.close()
        print("\nSerial ports closed.")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    hub_port = sys.argv[1]
    node_port = sys.argv[2]
    test_number = int(sys.argv[3]) if len(sys.argv) > 3 else 19

    print(f"\n{'='*60}")
    print(f"  ESP-NOW Multi-Device Test Runner")
    print(f"  HUB:  {hub_port}")
    print(f"  NODE: {node_port}")
    print(f"  Test: {test_number}")
    print(f"{'='*60}\n")

    run_test(hub_port, node_port, test_number)
