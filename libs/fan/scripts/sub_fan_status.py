import zmq
import json
import time

def main():
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect("ipc:///tmp/doly_fan_zmq.sock")
    socket.setsockopt_string(zmq.SUBSCRIBE, "fan_status")
    
    print("Waiting for fan_status messages on ipc:///tmp/doly_fan_zmq.sock...")
    try:
        while True:
            if socket.poll(2000):
                topic = socket.recv_string()
                payload = socket.recv_json()
                print(f"[{time.strftime('%H:%M:%S')}] {topic}: {json.dumps(payload, indent=2)}")
            else:
                print("No message received in 2 seconds...")
    except KeyboardInterrupt:
        print("Interrupted by user")
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    main()
