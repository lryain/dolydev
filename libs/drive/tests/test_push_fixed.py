#!/usr/bin/env python3
import zmq
import json
import time

context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.connect("ipc:///tmp/doly_control.sock")

# IMPORTANT: Wait for connection to establish!
print("✅ Connected to ipc:///tmp/doly_control.sock, waiting 0.5s...")
time.sleep(0.5)

payload = {
    "action": "enable_servo_left",
    "value": True,
    "timestamp": 1234567890,
    "source": "python_test"
}

print(f"📤 Sending: io.pca9535.control.enable_servo_left | {json.dumps(payload)}")
# Use send_string like quick_test.py does!
socket.send_string("io.pca9535.control.enable_servo_left", zmq.SNDMORE)
socket.send_string(json.dumps(payload))

print("✅ Sent! Waiting 2 seconds...")
time.sleep(2)

print("✅ Closing socket...")
socket.close()
print("✅ Terminating context...")
context.term()
print("✅ Done!")
