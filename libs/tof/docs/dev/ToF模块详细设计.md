# Tof模块详细设计

libs/tof 距离传感器模块

## 硬件设计
两颗 VL6180X 都挂载于 I2C6，其中一颗的使能脚 TOF_ENL 连接到 PCA9535 的 IO_6，另一颗永久使能。
由于两颗TOF传感器的默认地址都是 0x29，因此需要通过软件手段对其进行区分。
## 地址配置
总线同址情况下通过 TOF_ENL 分时上电配置不同地址后并挂，左右TOF地址为 0x29 与 0x2A。更改之后会设置一个“地址配置标志”：SETUP_MARKER = Path('/tmp/tof_demo_setup_done')，避免重复更改地址。TOF模块每次运行会检测这个标志，如果没有设置则进行地址更改！但是每次重新上电后都需要重新配置地址，因为TOF的地址不是永久修改的。这个我们的tof模块已经解决这个问题了。
## 校准
一般已经手动校准过了，校准数据保存在 libs/tof/data/tof_offsets.json中。然后TOF模块每次运行会加载这个校准数据，如果没有的话就需要进行校准！
## 模块介绍
参考手册：libs/tof/docs/VL6180X.pdf
libs/tof/arch/adafruit_vl6180x.py python驱动参考，如果我们想改成cpp驱动请参考这个
libs/tof/tof_en_ctl.cpp cpp的小工具用来配置TOF的地址，然后配置TOF地址的逻辑可以参考这个
libs/tof/tof_demo.py 是一个简单的python测试程序，可以用来测试TOF模块是否工作正常，运行时会根据是否存在SETUP_MARKER来决定是否进行地址配置。然后加载校准数据，最后进入循环读取左右TOF数据并打印。
libs/tof/tof_calibration.py 手动校准程序
libs/tof/tof_historytest.py Read the last 16 ranges from the history buffer as a List[int]
libs/tof/tof_alts.py 环境光检测待实现

需要实现一个C++版本的TOF驱动模块，集成到libs/drive中，提供距离数据的读取接口，并支持避障逻辑的实现。

## 问题

目前有个bug
使用continous模式读取TOF数据时，重复打开会卡死
可能是释放资源的问题

/home/pi/dolydev/.venv/bin/python tof_demo.py
/home/pi/dolydev/.venv/lib/python3.11/site-packages/adafruit_blinka/microcontroller/generic_linux/i2c.py:30: RuntimeWarning: I2C frequency is not settable in python, ignoring!
  warnings.warn(
当前左侧偏移量: 9 mm, 右侧偏移量: 86 mm
TOF demo running. Ctrl-C to exit.
L= 121 mm | R= 118 mm
L= 122 mm | R= 120 mm
L= 129 mm | R= 124 mm

开源
