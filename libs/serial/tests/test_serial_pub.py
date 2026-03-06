#!/usr/bin/env python3
import sys
import time
import argparse
import zmq


def parse_args():
    p = argparse.ArgumentParser(description='Serial service publish integration test')
    p.add_argument('--topic', type=str, default='event.audio.', help='ZMQ topic prefix to subscribe to (default event.audio.)')
    p.add_argument('--expected', type=int, default=5, help='Number of expected messages')
    p.add_argument('--timeout', type=int, default=7, help='Timeout seconds to wait for messages')
    p.add_argument('--no-start', action='store_true', help='If set, do not start serial_service, only subscribe to ZMQ')
    return p.parse_args()



def main():
    args = parse_args()
    topic = args.topic
    timeout = args.timeout

    # Prepare ZMQ subscriber
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.connect('ipc:///tmp/doly_serial_pub.sock')
    # subscribe to the provided topic prefix
    sock.setsockopt(zmq.SUBSCRIBE, topic.encode())
    sock.setsockopt(zmq.RCVTIMEO, 5000)
    # Give the socket a small time to connect and complete subscription handshake
    time.sleep(0.5)

    # This script is a pure ZeroMQ subscriber. Start serial_service manually if needed.

    # Try to collect messages
    start = time.time()
    try:
        # collect messages
        while True:
            try:
                parts = sock.recv_multipart()
                topic = parts[0].decode('utf-8', errors='ignore')
                data = parts[1].decode('utf-8', errors='ignore')
                print('SUB:', topic, data)
                got += 1
            except Exception as e:
                # timeout
                time.sleep(0.05)
                continue
    finally:
        pass

    print(f'Got {got} messages, ')
    sys.exit(1)


if __name__ == '__main__':
    main()
