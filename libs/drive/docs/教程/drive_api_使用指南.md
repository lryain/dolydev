# Drive API 使用指南

本文档面向 `libs/drive` 的使用者，说明 Drive 模块对外提供的两层接口：

1. 通过 ZMQ 发送 JSON 命令给 `drive_service`。
2. 直接调用 SDK 风格的 `DriveControl` 接口。

当前 Drive 服务的 ZMQ 端点默认是 `ipc:///tmp/doly_control.sock`，测试脚本使用的 topic 是 `io.pca9535.control`。

## 1. 通信方式

### 1.1 ZMQ 调用

客户端只需要向 ZMQ 端点发送两段字符串：

1. topic，例如 `io.pca9535.control`
2. JSON 命令体

示例：

```python
push.send_string("io.pca9535.control", zmq.SNDMORE)
push.send_string(json.dumps({"action": "motor_stop"}))
```

ZMQ 侧的核心字段都是 `action`。`drive_service` 会根据 `action` 决定走普通电机控制、精确运动控制、动画 API，还是 SDK 透传。

### 1.2 SDK 风格调用

服务内部同时支持直接调用 `DriveControl` 族接口，例如：

- `DriveControl::goXY(...)`
- `DriveControl::goDistance(...)`
- `DriveControl::goRotate(...)`
- `DriveControl::freeDrive(...)`
- `DriveControl::getPosition()`
- `DriveControl::resetPosition()`

这些接口在服务内部已经封装到 `MotorController`，因此外部既可以走 ZMQ，也可以走 SDK 风格封装。

## 2. 动作分类

`drive_service` 里和电机相关的动作大致分成四类：

1. 基础电机动作：`motor_forward`、`motor_backward`、`motor_turn_left`、`motor_turn_right`、`motor_stop`、`motor_brake`
2. 精确运动动作：`move_distance_cm`、`move_distance_cm_pid`、`turn_deg`、`turn_deg_pid`
3. 动画/高级动作：`drive_distance`、`drive_rotate`、`drive_rotate_left`、`drive_rotate_right`、`drive_distance_pid`、`turn_deg_pid_advanced`、`go_xy`
4. SDK 透传动作：`sdk_go_xy`、`sdk_go_distance`、`sdk_go_rotate`、`sdk_free_drive`、`sdk_get_position` 等

如果你只想快速控制机器人，优先使用第 2 类和第 3 类；如果你在移植上层 SDK 或需要和现有 SDK 参数完全一致，就使用第 4 类。

## 3. 基础电机 API

### 3.1 `motor_forward`

让左右轮同时前进。

参数：

- `speed`：速度，通常是 `0.0 ~ 1.0`
- `duration`：持续时间，单位秒；如果不传或为 `0`，则持续运行，直到调用停止命令

示例：

```json
{"action":"motor_forward","speed":0.3,"duration":1.0}
```

### 3.2 `motor_backward`

让左右轮同时后退。

参数与 `motor_forward` 相同。

示例：

```json
{"action":"motor_backward","speed":0.3,"duration":1.0}
```

### 3.3 `motor_turn_left`

原地左转。

参数：

- `speed`：转向速度，通常是 `0.0 ~ 1.0`
- `duration`：持续时间，单位秒

示例：

```json
{"action":"motor_turn_left","speed":0.25,"duration":0.8}
```

### 3.4 `motor_turn_right`

原地右转。

参数与 `motor_turn_left` 相同。

### 3.5 `motor_stop`

立即停止电机输出。

示例：

```json
{"action":"motor_stop"}
```

### 3.6 `motor_brake`

立即刹车。语义上比普通停止更偏向紧急制动。

示例：

```json
{"action":"motor_brake"}
```

### 3.7 `set_motor_speed`

直接设置左右轮速度。

参数：

- `left`：左轮速度，范围通常是 `-1.0 ~ 1.0`
- `right`：右轮速度，范围通常是 `-1.0 ~ 1.0`
- `duration`：持续时间，单位秒

示例：

```json
{"action":"set_motor_speed","left":0.2,"right":0.25,"duration":1.5}
```

### 3.8 `motor_move_pulses`

按编码器脉冲移动。

参数：

- `pulses`：目标脉冲数
- `throttle`：油门，通常 `0.0 ~ 1.0`
- `assume_rate`：估算速率
- `timeout`：超时，单位秒

示例：

```json
{"action":"motor_move_pulses","pulses":1200,"throttle":0.3,"assume_rate":100.0,"timeout":3.0}
```

### 3.9 `move_distance_cm`

按厘米移动。

参数：

- `distance_cm`：距离，单位厘米，正数通常表示前进，负数表示后退
- `throttle`：油门，通常 `0.0 ~ 1.0`
- `timeout_ms`：超时，单位毫秒

示例：

```json
{"action":"move_distance_cm","distance_cm":5.0,"throttle":0.3,"timeout_ms":10000}
```

## 4. 精确运动 API

这类 API 主要面向更稳定、更可控的移动控制，参数和 SDK 结构更接近。

### 4.1 `turn_deg`

按角度转向。

参数：

- `angle_deg`：角度，正数通常表示右转，负数通常表示左转
- `throttle`：油门，未显式传 `speed` 时会根据 `throttle` 推导速度
- `speed`：可直接指定速度，优先级高于 `throttle`
- `from_center`：是否按中心转向
- `to_forward`：运动方向控制，和角度方向配合使用
- `with_brake`：完成后是否刹车
- `acceleration_interval`：加减速间隔
- `control_speed`：是否启用速度闭环
- `control_force`：是否启用力控
- `timeout_ms`：超时，单位毫秒

示例：

```json
{
  "action": "turn_deg",
  "angle_deg": -30.0,
  "from_center": true,
  "speed": 28,
  "toForward": false,
  "with_brake": true,
  "acceleration_interval": 1,
  "control_speed": false,
  "control_force": true,
  "timeout_ms": 5000
}
```

说明：

- 如果没有传 `speed`，服务会根据 `throttle` 估算速度。
- `turn_deg` 最终会落到内部的 `go_rotate` 实现。

### 4.2 `move_distance_cm_pid`

基于 PID 的厘米级移动。

参数：

- `distance_cm` 或 `distance`：距离，单位厘米
- `speed`：速度
- `direction`：方向，`0` 通常表示后退，`1` 表示前进
- `timeout_ms`：超时，单位毫秒

示例：

```json
{"action":"move_distance_cm_pid","distance_cm":10.0,"speed":30,"direction":1,"timeout_ms":5000}
```

### 4.3 `turn_deg_pid`

基于 PID 的角度控制。

参数：

- `angle_deg`：角度
- `speed`：速度，支持小数或百分比风格
- `timeout_ms`：超时，单位毫秒

示例：

```json
{"action":"turn_deg_pid","angle_deg":90.0,"speed":0.3,"timeout_ms":5000}
```

## 5. 动画与高级 API

这一组是当前最常用的上层接口，参数风格和 SDK 接近，适合直接给动画系统、Blockly 或上位机调用。

### 5.1 `go_xy`

按二维坐标移动。

参数：

- `x`：X 轴目标值
- `y`：Y 轴目标值
- `speed`：速度，单位百分比，通常 `0 ~ 100`
- `toForward` 或 `to_forward`：是否朝前运动
- `with_brake`：是否刹车
- `acceleration_interval` 或 `accel_interval`：加减速间隔
- `control_speed`：是否启用速度控制
- `control_force`：是否启用力控
- `timeout_ms`：超时，单位毫秒

示例：

```json
{
  "action": "go_xy",
  "x": 100,
  "y": 50,
  "speed": 30,
  "toForward": true,
  "with_brake": true,
  "acceleration_interval": 3,
  "control_speed": true,
  "control_force": true,
  "timeout_ms": 12000
}
```

说明：

- 这个接口适合做平面位移类动作。
- 如果动作比较长，建议把 `timeout_ms` 留大一点，避免误判超时。

### 5.2 `drive_distance`

高级距离移动接口，已经补齐成 SDK 风格的完整参数。

参数：

- `distance_mm` 或 `distance`：距离，单位毫米
- `speed`：速度，单位百分比，通常 `0 ~ 100`
- `toForward` 或 `to_forward`：是否前进
- `with_brake` 或 `brake`：是否刹车
- `acceleration_interval`、`accel_interval` 或 `accel`：加减速间隔
- `control_speed`：是否启用速度控制
- `control_force`：是否启用力控
- `timeout_ms`：超时，单位毫秒
- `direction`：兼容参数，通常 `0` 和前进绑定，`1` 和后退绑定，服务内部会做兼容映射

示例：

```json
{
  "action": "drive_distance",
  "distance_mm": 100,
  "speed": 30,
  "toForward": true,
  "with_brake": true,
  "acceleration_interval": 2,
  "control_speed": true,
  "control_force": true,
  "timeout_ms": 7000
}
```

说明：

- 这是推荐的“高级距离移动”入口。
- 如果上层已经在使用毫米单位，优先用这个接口，而不是 `move_distance_cm`。

### 5.3 `drive_rotate`

高级转向接口，支持完整参数。

参数：

- `angle_deg` 或 `driveRotate`：旋转角度，单位度
- `speed`：速度，单位百分比
- `from_center`、`is_center` 或 `isCenter`：是否中心转向
- `toForward` 或 `to_forward`：方向控制
- `with_brake`：是否刹车
- `acceleration_interval`、`accel_interval`：加减速间隔
- `control_speed`：是否启用速度控制
- `control_force`：是否启用力控
- `timeout_ms`：超时，单位毫秒

示例：

```json
{
  "action": "drive_rotate",
  "angle_deg": 45.0,
  "from_center": true,
  "speed": 30,
  "toForward": true,
  "with_brake": true,
  "acceleration_interval": 2,
  "control_speed": false,
  "control_force": true,
  "timeout_ms": 5000
}
```

说明：

- 如果 `from_center` 为真，服务内部会对速度做一定增强，以保证中心转向更稳定。
- 这个接口比 `turn_deg` 更适合作为动画系统的标准入口。

### 5.4 `drive_rotate_left`

左转便捷接口。

参数：

- `angle_deg` 或 `driveRotate`：角度
- `speed`：速度
- `is_center` 或 `isCenter`：是否中心转向
- `timeout_ms`：超时

示例：

```json
{"action":"drive_rotate_left","angle_deg":90.0,"speed":20,"is_center":true,"timeout_ms":8000}
```

### 5.5 `drive_rotate_right`

右转便捷接口。

参数与 `drive_rotate_left` 相同。

### 5.6 `drive_distance_pid`

PID 版本的高级距离接口。

参数：

- `distance_mm` 或 `distance`：距离
- `speed`：速度
- `accel`：加速度参数
- `brake`：制动参数
- `direction`：方向
- `timeout_ms`：超时

示例：

```json
{"action":"drive_distance_pid","distance_mm":150,"speed":20,"accel":0,"brake":0,"direction":0,"timeout_ms":10000}
```

### 5.7 `turn_deg_pid_advanced`

PID 版本的高级转向接口。

参数：

- `angle_deg`：角度
- `speed`：速度
- `is_center`：是否中心转向
- `timeout_ms`：超时

示例：

```json
{"action":"turn_deg_pid_advanced","angle_deg":90.0,"speed":20,"is_center":true,"timeout_ms":8000}
```

## 6. SDK 透传 API

这组接口直接对应 `DriveControl` 的 SDK 方法，适合做兼容测试或直接对接旧 SDK 调用习惯。

### 6.1 `sdk_go_xy`

直接透传到 `DriveControl::goXY()`。

参数：

- `id`：命令 ID
- `x`：X 坐标
- `y`：Y 坐标
- `speed`：速度
- `toForward`：是否前进
- `with_brake`：是否刹车
- `accel_interval`：加减速间隔
- `control_speed`：是否启用速度控制
- `control_force`：是否启用力控

示例：

```json
{"action":"sdk_go_xy","id":1000,"x":100,"y":50,"speed":30,"toForward":true,"with_brake":false,"accel_interval":0,"control_speed":false,"control_force":true}
```

### 6.2 `sdk_go_distance`

直接透传到 `DriveControl::goDistance()`。

参数：

- `id`：命令 ID
- `mm`：距离，单位毫米
- `speed`：速度
- `toForward`：是否前进
- `with_brake`：是否刹车
- `accel_interval`：加减速间隔
- `control_speed`：是否启用速度控制
- `control_force`：是否启用力控

示例：

```json
{"action":"sdk_go_distance","id":1001,"mm":50,"speed":35,"toForward":false}
```

### 6.3 `sdk_go_rotate`

直接透传到 `DriveControl::goRotate()`。

参数：

- `id`：命令 ID
- `rotateAngle`：旋转角度
- `from_center`：是否中心转向
- `speed`：速度
- `toForward`：是否前进
- `with_brake`：是否刹车
- `accel_interval`：加减速间隔
- `control_speed`：是否启用速度控制
- `control_force`：是否启用力控

示例：

```json
{"action":"sdk_go_rotate","id":1002,"rotateAngle":45.0,"from_center":true,"speed":30,"toForward":true}
```

### 6.4 其他 SDK 相关动作

常见查询和控制动作还包括：

- `sdk_free_drive`：手动自由驱动
- `sdk_is_active`：查询驱动是否激活
- `sdk_abort`：中止当前动作
- `sdk_get_position`：读取当前位置
- `sdk_reset_position`：重置位姿
- `sdk_get_state`：读取驱动状态
- `sdk_get_rpm`：读取轮速
- `sdk_get_version`：读取版本号

这些动作更适合调试、兼容或做上层状态同步。

## 7. `MotorController` 直接调用方式

如果你在 C++ 内部直接使用 `MotorController`，常用方法如下：

- `forward(speed, duration)`：前进
- `backward(speed, duration)`：后退
- `turnLeft(speed, duration)`：左转
- `turnRight(speed, duration)`：右转
- `stop()`：停止
- `brake()`：刹车
- `move_distance_cm(distance_cm, throttle, timeout_ms)`：厘米级移动
- `turn_deg(angle_deg, throttle, timeout_ms)`：角度转向
- `go_xy(x, y, speed, to_forward, with_brake, acceleration_interval, control_speed, control_force, timeout_ms)`：二维移动
- `go_distance(distance_mm, speed, to_forward, with_brake, acceleration_interval, control_speed, control_force, timeout_ms)`：毫米级移动
- `go_rotate(angle_deg, from_center, speed, to_forward, with_brake, acceleration_interval, control_speed, control_force, timeout_ms)`：角度转向
- `drive_distance(...)`：动画系统距离接口
- `drive_rotate(...)`：动画系统转向接口

建议规则：

1. 如果你在做简单控制，先用 `forward`、`backward`、`turnLeft`、`turnRight`。
2. 如果你需要距离或角度精度，优先用 `move_distance_cm`、`turn_deg`、`drive_distance`、`drive_rotate`。
3. 如果你要和 SDK 保持同一套参数，优先用 `go_xy`、`go_distance`、`go_rotate`。

## 8. 常见参数约定

### 8.1 方向参数

不同接口里的方向参数含义略有差异：

- `toForward` / `to_forward`：明确控制是前进还是后退
- `direction`：通常 `0` 和前进或后退之一绑定，具体要看接口定义
- `angle_deg`：正负号本身也会影响转向方向

如果你不确定方向映射，建议显式传 `toForward`，不要只依赖角度正负。

### 8.2 速度参数

- 基础电机 API 通常使用 `0.0 ~ 1.0`
- 动作和 SDK 类 API 通常使用 `0 ~ 100`

这是最容易混淆的地方。写调用时要先确认当前接口期望的是“油门比例”还是“百分比速度”。

### 8.3 超时参数

- `timeout_ms`：毫秒
- `timeout`：某些旧接口可能使用秒

建议统一优先使用 `timeout_ms`。

### 8.4 加减速参数

- `acceleration_interval`
- `accel_interval`
- `accel`

这几个字段在不同接口里都能见到，含义基本相近。为了兼容历史调用，服务已经做了别名适配。

## 9. 典型使用场景

### 9.1 简单前进 1 秒

```json
{"action":"motor_forward","speed":0.3,"duration":1.0}
```

### 9.2 前进 10 厘米

```json
{"action":"move_distance_cm","distance_cm":10.0,"throttle":0.3,"timeout_ms":5000}
```

### 9.3 原地右转 90 度

```json
{"action":"turn_deg","angle_deg":90.0,"speed":30,"from_center":true,"timeout_ms":5000}
```

### 9.4 高级毫米级前进

```json
{"action":"drive_distance","distance_mm":150,"speed":30,"toForward":true,"with_brake":true,"timeout_ms":7000}
```

### 9.5 SDK 兼容旋转

```json
{"action":"sdk_go_rotate","id":1002,"rotateAngle":45.0,"from_center":true,"speed":30,"toForward":true}
```

## 10. 测试与验证

仓库里已经提供了固定回归脚本：

- `libs/drive/tests/quick_test_sdk_api.py`

它会按固定参数发送以下回归用例：

- `go_xy`
- `drive_rotate`
- `turn_deg`
- `drive_distance`
- `sdk_go_rotate`
- `sdk_go_distance`
- `sdk_go_xy`

建议验证步骤：

1. 启动或 redeploy drive service。
2. 运行回归脚本。
3. 查看服务日志，确认每个动作都打印 `success=YES` 或 SDK 返回 `true`。

如果 `go_xy` 这类动作比较耗时，建议适当放大 `timeout_ms`，避免把正常动作误判成超时失败。

## 11. 常见问题

### 11.1 动作发出后没有反应

先确认：

1. `drive_service` 是否已启动。
2. ZMQ 端点是否是 `ipc:///tmp/doly_control.sock`。
3. `action` 名字是否拼对。
4. 参数单位是否正确。

### 11.2 速度太小，电机不动

低速时电机可能克服不了静摩擦。当前实现里已经给部分动作做了最低速度保护，但如果速度过低，仍然可能看起来“不动”。建议把速度提高到更稳定的区间再测。

### 11.3 `turn_deg` 和 `drive_rotate` 该选哪个

- 只想做角度控制，并且调用风格更接近传统“转向”概念时，用 `turn_deg`
- 需要更完整的动画参数、中心转向控制，或者想和 SDK 行为一致时，用 `drive_rotate`

### 11.4 `move_distance_cm` 和 `drive_distance` 该选哪个

- 上层已经是厘米单位，且调用较简单时，用 `move_distance_cm`
- 上层是毫米单位，或者希望完整参数和 SDK 风格一致时，用 `drive_distance`

## 12. 推荐调用顺序

如果你是新接入的上层模块，推荐按这个顺序选接口：

1. 优先 `drive_distance`、`drive_rotate`、`go_xy`。
2. 兼容旧调用时使用 `turn_deg`、`move_distance_cm`。
3. 如果你要和现有 SDK 一一对齐，用 `sdk_go_distance`、`sdk_go_rotate`、`sdk_go_xy`。

这样可以最大程度减少后续接口风格不一致的问题。
