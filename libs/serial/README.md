# 串口服务
该模块提供一个串口监听服务，从串口设备读取字节并将映射的事件发布到 ZeroMQ 总线上。

## 配置
将配置放在 serial.yaml 中。例如：

你可以使用命令行参数覆盖配置文件：

--dev <device>：设置串口设备
--baud <baud>：设置波特率
--config <path>：指定备用配置文件
--no-config：忽略配置文件

## 部署
使用 scripts/manage_serial_service.sh 管理脚本安装和管理 systemd 服务。

## 调试与测试
使用 scripts/manage_serial_service.sh status 查看服务状态。
使用 scripts/manage_serial_service.sh logs 查看日志。
对于 CI 和本地测试，可以使用 tests/test_serial_publish.py，它会订阅 ZeroMQ 总线并验证消息是否被发布。
