import threading
import time

import doly_servo as servo

try:
    import doly_helper as helper
except ImportError:
    helper = None


complete_event = threading.Event()


def enum_name(value):
    return getattr(value, "name", str(value))


def try_stop_service():
    if helper is None:
        print("[warn] doly_helper is not installed; stop drive-service.service manually if init fails.")
        return

    rc = helper.stop_doly_service()
    if rc < 0:
        print(f"[warn] helper.stop_doly_service returned {rc}")


def wait_for_completion(timeout=4.0):
    finished = complete_event.wait(timeout)
    complete_event.clear()
    return finished


def on_complete(cmd_id, channel):
    print(f"[event] complete id={cmd_id} channel={enum_name(channel)}")
    complete_event.set()


def on_abort(cmd_id, channel):
    print(f"[event] abort id={cmd_id} channel={enum_name(channel)}")


def on_error(cmd_id, channel):
    print(f"[event] error id={cmd_id} channel={enum_name(channel)}")


def main():
    print(f"[info] ServoControl version: {servo.get_version():.3f}")
    try_stop_service()

    servo.on_complete(on_complete)
    servo.on_abort(on_abort)
    servo.on_error(on_error)

    rc = servo.init()
    if rc < 0:
        print(f"[error] ServoControl.init failed: {rc}")
        print("[error] Ensure drive-service.service is stopped and hardware is connected.")
        return 1

    try:
        print("\n--- Basic API demo ---")
        servo.set_servo(1, servo.ServoId.Servo0, 60.0, 50, False)
        wait_for_completion()

        servo.set_servo(2, servo.ServoId.Servo1, 120.0, 35, False)
        wait_for_completion()

        print("\n--- Invert parameter demo ---")
        servo.set_servo(3, servo.ServoId.Servo1, 45.0, 45, True)
        wait_for_completion()

        print("\n--- Abort demo ---")
        servo.set_servo(4, servo.ServoId.Servo0, 150.0, 15, False)
        time.sleep(0.3)
        servo.abort(servo.ServoId.Servo0)
        time.sleep(0.2)

        print("\n--- Release demo ---")
        servo.release(servo.ServoId.Servo0)
        servo.release(servo.ServoId.Servo1)
        time.sleep(0.2)
        return 0
    finally:
        servo.dispose()
        time.sleep(0.2)


if __name__ == "__main__":
    raise SystemExit(main())