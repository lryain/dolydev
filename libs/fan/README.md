# Fan Temperature Control Service

这是一个用于Raspberry Pi的智能风扇温度控制常驻服务，使用PCA9685 PWM控制器管理风扇转速。

设置配置文件路径
# 指定启动参数  -c "/home/pi/dolydev/config/fan_config.yaml"
ExecStart=/usr/local/bin/fan_service -c "/home/pi/dolydev/config/fan_config.yaml"
修改后重新部署服务
./manage_service.sh redeploy
TODO:
部署的时候需要从环境变量中读取配置文件路径，而不是写死

## 🚀 部署状态

✅ **服务已成功部署并运行**
- 服务状态: `Active (running)`
- 运行时间: 持续监控中
- 智能保护: 已激活并正常工作
- 日志记录: 正常输出到 `/home/pi/noradev/logs/`

**快速验证:**
```bash
sudo systemctl status fan-control
```

## 功能特性

- **智能温度控制**: 根据CPU温度自动调节风扇转速
- **线性PWM调节**: 在设定温度范围内线性调节风扇转速
- **智能保护**: 检测温度上升趋势，自动启用最大风扇转速
- **常驻服务**: 使用systemd管理，支持自动启动和重启
- **配置热重载**: 支持SIGHUP信号重载配置，无需重启服务
- **详细日志**: 记录温度、PWM值和系统事件
- **错误恢复**: 自动重启机制，确保服务稳定性

## 安装和使用

### 1. 构建服务

```bash
cd /home/pi/noradev/factory_test/scripts/fan
./manage_service.sh build
```

### 2. 安装服务

```bash
sudo ./manage_service.sh install
```

### 3. 启动服务

```bash
sudo ./manage_service.sh start
```

### 4. 检查状态

```bash
./manage_service.sh status
```

## 服务管理

### 基本命令

```bash
# 启动服务
sudo ./manage_service.sh start

# 停止服务
sudo ./manage_service.sh stop

# 重启服务
sudo ./manage_service.sh restart

# 查看状态
./manage_service.sh status

# 查看日志
./manage_service.sh logs

# 实时监控日志
./manage_service.sh monitor
```

### 配置管理

服务使用配置文件 `/home/pi/dolydev/config/fan_config.yaml`。

修改配置后，可以通过以下方式重载：

```bash
# 重载配置 (推荐)
sudo ./manage_service.sh reload

# 或重启服务
sudo ./manage_service.sh restart
```

### 卸载服务

```bash
sudo ./manage_service.sh uninstall
```

## 配置参数

配置文件格式：

```
# PWM values for different temperatures
0
500
1000
1500
2000
2500
3000
3500
4000

# Temperature config
fan_start_temp: 45
fan_stop_temp: 40
pwm_min: 500
pwm_max: 4000
temp_min: 45
temp_max: 70
```

### 参数说明

- `fan_start_temp`: 风扇开始转动的温度 (°C)
- `fan_stop_temp`: 风扇停止转动的温度 (°C)
- `pwm_min`: 最小PWM值 (0-4095)
- `pwm_max`: 最大PWM值 (0-4095)
- `temp_min`: 温度控制起始点 (°C)
- `temp_max`: 温度控制结束点 (°C)

## 工作原理

1. **温度监控**: 每秒读取 `/sys/class/thermal/thermal_zone0/temp`
2. **PWM计算**: 根据当前温度计算合适的PWM值
3. **智能保护**: 每10秒检查温度趋势，如果温度持续上升则强制最大PWM
4. **日志记录**: 每30秒或状态变化时记录日志

### 温度控制逻辑

- 温度 < `fan_stop_temp`: 风扇停止 (PWM = 0)
- `fan_stop_temp` ≤ 温度 < `fan_start_temp`: 最小转速 (PWM = `pwm_min`)
- `fan_start_temp` ≤ 温度 < `temp_max`: 线性调节
- 温度 ≥ `temp_max`: 最大转速 (PWM = `pwm_max`)

## 日志和监控

### 日志位置

- 系统日志: `journalctl -u fan-control`

### 监控服务

```bash
# 查看最近的日志
sudo journalctl -u fan-control -n 20

# 实时监控
sudo journalctl -u fan-control -f

# 查看特定时间段的日志
sudo journalctl -u fan-control --since "1 hour ago"
```

