# TOF (VL6180X) 测试与巡航

本目录包含：
- C++: `tof_en_ctl.cpp`（通过 Doly GPIO 驱动控制 TOF_ENL 使能线）
- Python: `tof_demo.py`（左右测距实时打印），`tof_cruise.py`（简单避障巡航）
- Python 服务: `tof_service.py`（被动查询式 ZMQ 服务），`tof_query.py`（查询客户端）
- 公共工具: `tof_common.py`（`run_en`、`load_offsets`、连续模式收尾等通用函数）
- 构建脚本：`build.sh`，运行脚本：`run_demo.sh`，`run_cruise.sh`
- 配置：`../../config/tof_cruise.ini`

注意：
- TOF_ENL 连接在 PCA9535 IO1_6（PinId=64）。右侧 TOF 无使能线，视为常开。
- I2C 总线为 CM4 I2C6（/dev/i2c-6）。
- Python 使用 `vl6180x-multi` 并做了 None-占位兼容（在脚本内部处理）。
- 需要 root 或具备相应 I2C/GPIO 访问权限。

## 被动查询式服务

新的 ZMQ 服务默认读取 [config/tof_service.yaml](/home/pi/dolydev/config/tof_service.yaml)，按请求执行单次 TOF 采样，不再常驻连续测距。

启动：

```bash
bash /home/pi/dolydev/libs/tof/run_service.sh
```

查询：

```bash
source /home/pi/DOLY-DIY/venv/bin/activate
python /home/pi/dolydev/libs/tof/tof_query.py --cmd ping
python /home/pi/dolydev/libs/tof/tof_query.py --cmd get_tof
python /home/pi/dolydev/libs/tof/tof_query.py --cmd get_snapshot --include-lux
```

Mock 测试：

```bash
source /home/pi/DOLY-DIY/venv/bin/activate
python /home/pi/dolydev/libs/tof/tests/test_tof_service_mock.py
```

真机测试：

```bash
/home/pi/dolydev/.venv/bin/python /home/pi/dolydev/libs/tof/tests/test_tof_service_real.py
```


## 逻辑梳理

## 测试命令
source /home/pi/.venv/factory-test/bin/activate
python run_demo.py

1. 刚上电，还未运行测试
gpioget gpiochip2 14
0

i2cdetect -y 6
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
20: -- -- -- -- -- -- -- -- -- 29 -- -- -- -- -- --

2. 开始更改其中一颗传感器地址
2.1 先关闭左侧，使只有右侧在线，将右侧改到 0x2A
run_en('off')
# 用 gpioget gpiochip2 15 命令检测 TOF_ENL 应该返回0
right._write_8(0x212, ADDR_RIGHT)

print("已将右侧改到 0x2A, 此时执行 i2cdetect -y 6 应检测到 0x2A")

在重启后，运行一次会进行地址更新，但是重复运行程序，需要在执行前先拉高一下那个被拉低的引脚

## bug

需要对factory_test/scripts/tof/tof_en_ctl.cpp做单独测试，要求：
没用运行程序之前用gpioget gpiochip2 14 检测TOF_ENL引脚应该返回0
在设置了run_en('off')之后
用gpioget gpiochip2 15 检测TOF_ENL引脚仍然返回0
在设置了run_en('off')之后
用gpioget gpiochip2 15 检测TOF_ENL引脚仍然返回0