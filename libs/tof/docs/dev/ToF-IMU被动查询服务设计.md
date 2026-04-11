# Doly ToF-IMU 被动查询服务设计

## 1. 目标

本次重构的目标不是再做一个“常驻高频轮询、职责混杂”的传感器服务，而是做一个面向自动回充的轻量传感器接入层：

1. TOF 采用 Python 实现并封装为 ZMQ REQ/REP 服务，按请求单次采样，降低空闲资源占用。
2. 服务协议统一预留 TOF 与 IMU 两类数据，但当前实现优先把 TOF 做扎实，IMU 有cpp API不需要做。
3. autocharge 侧不再依赖硬编码阈值，关键参数统一收敛到 config。
4. 当 drive 共享状态不可用时，autocharge 可以回退到 ZMQ TOF 服务继续获取左右测距。

## 2. 当前问题

现有 libs/tof 更像测试脚本集合，而不是一个稳定服务：

1. 接口分散在 demo、continuous、history、als 等脚本中，没有统一协议。
2. continuous 模式重复打开存在卡死风险，说明资源生命周期不稳定。
3. 缺少统一配置入口，地址、GPIO、阈值、缓存策略都散落在脚本里。
4. 对 autocharge 而言，缺少一个明确的“按需取一次左右 TOF 快照”的能力。

## 3. 设计原则

1. 被动查询优先：无请求就不做测距。
2. 单次采样优先：默认不进入 continuous 模式，规避重复打开卡死。
3. 小缓存兜底：允许 50 到 60ms 的短缓存，避免短时间重复请求把 I2C 打满。
4. 空闲自动释放：超过 idle_close_ms 后主动关闭 I2C 句柄，减少长期持有硬件资源。
5. 协议先稳后广：先稳定 get_tof、get_snapshot、health，再扩展事件流或更多传感器。

## 4. 架构

```
autocharge / 调试工具
        |
        | ZMQ REQ/REP
        v
  tof_service.py
    |-- TofHardware      -> VL6180X 单次采样
    |-- ImuProvider      -> 当前预留，Mock 已实现
    |-- Config Loader    -> /home/pi/dolydev/config/tof_service.yaml
    |-- Idle Manager     -> 空闲自动关闭 I2C
```

### 4.1 TOF 数据链路

1. 请求到达服务。
2. 服务检查短缓存是否仍有效。
3. 若缓存失效，则懒加载 I2C 与左右 VL6180X。
4. 执行单次 range 读取；若请求要求 include_lux，则额外读取 ALS。
5. 计算 min_distance_mm、balance_error_mm、obstacle_detected 后返回 JSON。

### 4.2 IMU 策略

当前实现不在 Python 服务里重新做 IMU 驱动，原因是：

1. autocharge 当前主路径已经可通过 drive 共享状态或底盘闭环使用 IMU 能力。
2. 本轮核心风险点在 TOF 服务化，而不是 IMU 原始驱动。
3. 若后续需要统一到同一总线，可新增 ImuProvider 适配器，把现有 SDK/共享状态/ZMQ AHRS 接入进来。

因此本次协议保留 get_imu 与 get_snapshot.imu 字段，但在真机模式下默认返回 not_implemented；Mock 模式会返回模拟姿态数据，便于联调。

## 5. 协议

端点：默认 tcp://127.0.0.1:5568

请求示例：

```json
{"cmd":"get_tof"}
{"cmd":"get_tof","include_lux":true}
{"cmd":"get_snapshot"}
{"cmd":"health"}
```

响应示例：

```json
{
  "ok": true,
  "timestamp_ms": 1710000000000,
  "tof": {
    "valid": true,
    "left_valid": true,
    "right_valid": true,
    "left_mm": 118,
    "right_mm": 121,
    "min_distance_mm": 118,
    "balance_error_mm": -3,
    "obstacle_detected": true,
    "source": "vl6180x",
    "cached": false
  }
}
```

## 6. 配置

配置文件位于 [config/tof_service.yaml](/home/pi/dolydev/config/tof_service.yaml)。

关键参数：

1. bind_endpoint：服务监听地址。
2. idle_close_ms：空闲多久后释放 I2C。
3. cache_ttl_ms：短缓存时间，用于压制突发重复请求。
4. auto_setup：是否自动执行 TOF 地址初始化。
5. obstacle_threshold_mm：供 autocharge 与避障共享的基础障碍阈值。
6. read_lux_by_default 与 als_gain：决定是否默认附带光照值。

## 7. autocharge 接入方案

本轮同步做了两件事：

1. [config/autocharge.yaml](/home/pi/dolydev/config/autocharge.yaml) 新增 autocharge 配置，把充电电压、电流阈值、旋转速度、前进速度、倒车速度、旋转补偿等参数外提。
2. autocharge 的 TofMonitor 先读共享状态，失败后自动回退到 ZMQ TOF 服务。

这样做的收益是：

1. drive 正常时，仍走零拷贝共享状态，最低延迟。
2. drive 侧 TOF 状态不可用时，autocharge 仍可通过 Python 服务继续工作。
3. 配置切换不需要重新编译 C++ 常量。

## 8. 测试方案

### 8.1 Mock 烟测

使用 [libs/tof/tests/test_tof_service_mock.py](/home/pi/dolydev/libs/tof/tests/test_tof_service_mock.py) 启动 mock 服务并验证 ping、get_tof、get_snapshot、health。

### 8.2 真机测试

1. 启动服务：bash /home/pi/dolydev/libs/tof/run_service.sh
2. 查询：python /home/pi/dolydev/libs/tof/tof_query.py --cmd get_tof
3. 联调 autocharge：启动 autocharge_service，确认在共享状态缺失时仍可拿到左右 TOF。

## 9. 后续增强

1. 增加真正的 IMU Provider，把共享状态或 AHRS ZMQ 订阅纳入统一服务。
2. 可选增加 PUB 状态广播，供调试面板订阅，但默认关闭，避免违背低资源目标。
3. 增加 systemd 单元与守护脚本。
4. 为自动回充倒车阶段增加 get_alignment_hint 接口，直接输出左右差值与推荐修正量。