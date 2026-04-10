"""
example.py

它演示了：
- 初始化驱动控制器
- 注册静态事件监听器
- 使用多种选项发送驱动命令
- 读取当前估算位置
- 清理资源

"""


import subprocess
import time
import doly_drive as drive
import doly_helper as helper


def on_drive_complete(cmd_id: int):
    print(f"[info] Drive complete id={cmd_id}")


def on_drive_error(cmd_id: int, side, err_type):
    # side 是 DriveErrorSide 枚举，err_type 是 DriveErrorType 枚举
    print(f"[error] Drive error id={cmd_id} side={int(side)} type={int(err_type)}")


def on_drive_state_change(drive_type, state):
    print(f"[info] Drive state type={int(drive_type)} state={int(state)}")


def stop_conflicting_drive_service() -> bool:
    """Stop the systemd service and any manually started drive_service process."""
    if helper.stop_doly_service() < 0:
        return False

    manual_service_paths = [
        "/home/pi/dolydev/libs/drive/build/drive_service",
        "drive_service",
    ]

    for service_path in manual_service_paths:
        subprocess.run(["pkill", "-f", service_path], check=False)

    time.sleep(0.2)
    return True


def main():
    # 版本信息
    try:
        print(f"[info] DriveControl Version: {drive.get_version():.3f}")
    except AttributeError:
        pass

    # 检查模块是否已初始化
    if drive.is_active():
        print("[info] DriveControl 已经处于激活状态")

    # *** 重要提示 *** 
    # 如果 doly drive 服务正在运行，请先停止它，
    # 否则库实例会产生冲突	
    if not stop_conflicting_drive_service():
        print("[error] Doly service stop failed")
        return -1
    time.sleep(2)  # 确保服务已完全停止
    # 注册静态事件监听器 
    drive.on_complete(on_drive_complete)
    drive.on_error(on_drive_error)
    drive.on_state_change(on_drive_state_change)

    # 初始化 DriveControl，可以使用校准后的 IMU 偏移量（gx, gy, gz, ax, ay, az）
    # 默认值为 0。为了获得更好的性能，请提供实际校准后的 IMU 偏移。
    # 详情请查阅 examples_helper.py
    rc = drive.init(0, 0, 0, 0, 0, 0)
    if rc != 0:
        print(f"[error] DriveControl 初始化失败，状态码: {rc}")
        return -2

    # 重置当前位置估算为 (0, 0, 0)
    drive.reset_position()

    speed = 60  # 示例速度 % (0..100)

    # 示例 1: 向前行驶 100mm，结束时刹车
    # 参数: id, 距离(mm), 速度, 是否前向, 是否刹车, 加速间隔, 是否动控制速度, 是否控制力矩
    # drive.go_distance(1, 50, speed, True, True, 0, False, True)
    # while drive.get_state() == drive.DriveState.Running:
    #     time.sleep(0.05)
    # drive.go_distance(1, 50, speed, False, True, 0, False, True)
    # while drive.get_state() == drive.DriveState.Running:
    #     time.sleep(0.05)

    # 示例 2: 在轮子上逆时针旋转 45 度，结束时刹车
    # 参数: id, 角度, 是否从中心旋转, 速度, 是否前向, 是否刹车
    drive.go_rotate(2, -45.0, True, speed, True, True)
    while drive.get_state() == drive.DriveState.Running:
        time.sleep(0.05)
    
    drive.go_rotate(2, 45.0, True, speed, True, True)
    while drive.get_state() == drive.DriveState.Running:
        time.sleep(0.05)

    # 示例 3: 行驶到 X= -100mm, Y=200mm (使用整数坐标), 以前向加速方式接近，结束时不刹车
    # Python 绑定要求 x 和 y 为整数。传递浮点数会导致 TypeError。
    # 请按如下所示转换或使用整数。
    # drive.go_xy(1, 10, 50, speed, True, False, 60)
    # while drive.get_state() == drive.DriveState.Running:
    #     time.sleep(0.05)
    # drive.go_xy(1, 100, -50, speed, True, False, 50)
    # while drive.get_state() == drive.DriveState.Running:
    #     time.sleep(0.05)
    # drive.go_xy(1, 30, 30, speed, True, False, 40)
    
    # 演示中止操作 (可选)
    # time.sleep(0.5)
    # drive.abort() 

    # 示例 4: 低级驱动控制，左右轮以指定速度向前行驶
    # 参数: 速度, 是否左轮, 是否前向
    # drive.free_drive(speed, False, True)  # 右轮驱动
    # drive.free_drive(speed, True, True)   # 左轮驱动
    # time.sleep(1.0)
    # drive.free_drive(0, False, True)
    # drive.free_drive(0, True, True)

    # 等待完成（简单循环示例）
    while drive.get_state() == drive.DriveState.Running:
        time.sleep(0.05)

    # 获取最终位置
    pos = drive.get_position()
    print(f"[info] Robot pos x={pos.x} y={pos.y} head={pos.head}")

    # 清理资源并执行 clear_listeners
    drive.clear_listeners()
    drive.dispose(True)  # 参数为 True 表示同时关闭 IMU 模块

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
