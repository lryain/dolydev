import time

import doly_arm as arm

try:
    import doly_helper as helper
except ImportError:
    helper = None


ENABLE_COMPLEX_ROUTINES = False


def enum_name(value):
    return getattr(value, "name", str(value))


def try_stop_service():
    if helper is None:
        print("[warn] doly_helper is not installed; stop drive-service.service manually if init fails.")
        return

    rc = helper.stop_doly_service()
    if rc < 0:
        print(f"[warn] helper.stop_doly_service returned {rc}")


def wait_until_idle(side, timeout=8.0):
    deadline = time.time() + timeout
    state = arm.get_state(side)
    while state == arm.ArmState.Running and time.time() < deadline:
        time.sleep(0.05)
        state = arm.get_state(side)
    return state


def dump_angles(side):
    for item in arm.get_current_angle(side):
        print(f"[info] side={enum_name(item.side)} angle={item.angle:.2f}")


def on_complete(cmd_id, side):
    print(f"[event] complete id={cmd_id} side={enum_name(side)}")


def on_error(cmd_id, side, error_type):
    print(
        f"[event] error id={cmd_id} side={enum_name(side)} type={enum_name(error_type)}"
    )


def on_state_change(side, state):
    print(f"[event] state side={enum_name(side)} state={enum_name(state)}")


def on_movement(side, delta):
    if abs(delta) >= 1.0:
        print(f"[event] movement side={enum_name(side)} delta={delta:.2f}")


def run_basic_demo():
    print("\n--- Basic API demo ---")
    rc = arm.set_angle(1, arm.ArmSide.Both, 50, 60, False)
    if rc < 0:
        raise RuntimeError(f"set_angle failed: {rc}")
    state = wait_until_idle(arm.ArmSide.Both)
    print(f"[info] basic demo finished with state={enum_name(state)}")
    dump_angles(arm.ArmSide.Both)


def run_advanced_demo():
    print("\n--- Advanced API demo ---")

    arm.move_multi_duration(
        {
            arm.ArmSide.Left: 45.0,
            arm.ArmSide.Right: 135.0,
        },
        1200,
    )
    time.sleep(1.5)

    arm.servo_swing_of(arm.ArmSide.Left, 90.0, 55, 15.0, 45, 2)
    time.sleep(2.0)

    arm.start_swing(arm.ArmSide.Right, 70.0, 110.0, 350, 2)
    time.sleep(2.0)

    if ENABLE_COMPLEX_ROUTINES:
        arm.lift_dumbbell(arm.ArmSide.Left, 30.0, 2)
        arm.wave_flag(arm.ArmSide.Right, 20.0, 3)
        arm.beat_drum(arm.ArmSide.Left, 15.0, 2)
        arm.paddle_row(arm.ArmSide.Right, 25.0, 2)
        arm.dual_paddle_row(20.0, 2)
        arm.dumbbell_dance(20.0, 2.0)
    else:
        print("[info] complex routines are disabled by default; set ENABLE_COMPLEX_ROUTINES=True to run them.")


def main():
    print(f"[info] ArmControl version: {arm.get_version():.3f}")
    print(f"[info] ArmControl max angle: {arm.get_max_angle()}")

    try_stop_service()

    arm.on_complete(on_complete)
    arm.on_error(on_error)
    arm.on_state_change(on_state_change)
    arm.on_movement(on_movement)

    rc = arm.init()
    if rc < 0:
        print(f"[error] ArmControl.init failed: {rc}")
        print("[error] Ensure drive-service.service is stopped and hardware is connected.")
        return 1

    try:
        print(f"[info] is_active={arm.is_active()}")
        time.sleep(0.5)
        run_basic_demo()
        run_advanced_demo()

        print("\n--- Return to neutral position ---")
        arm.set_angle(2, arm.ArmSide.Both, 50, 90, True)
        wait_until_idle(arm.ArmSide.Both)
        dump_angles(arm.ArmSide.Both)
        return 0
    finally:
        arm.dispose()
        time.sleep(0.2)


if __name__ == "__main__":
    raise SystemExit(main())