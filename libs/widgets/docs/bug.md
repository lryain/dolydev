# 测试倒计时功能出现bug
使用命令：
(venv) pi@raspberrypi:~/dolydev/libs/widgets/python $ python3 test_widget_service.py timer_start 12
开启一个12秒的倒计时:

"cmd.widget.timer.start", {
    "action": "start",
    "widget_id": "timer",
    "mode": "countdown",
    "duration_sec": duration_sec,
    "timeout_ms": 1000,  # 1秒后自动隐藏
}

可以看到指定了timeout_ms 为1秒也就是1秒后倒计时会隐藏，但是根据配置libs/widgets/config/widgets.default.json里的"auto_show_remaining_sec": 5配置，应当是在还剩下5秒的时候自动将倒计时widget显示出来才对
但是目前没能恢复显示，而是在1秒后就直接彻底清除掉了timer widget，正确的应当是在1秒后只是隐藏timer widget的显示，然后后台依然在运行才对！
## 涉及模块
libs/widgets 复杂显示和隐藏以及调用libs/EyeEngine模块的库来完成全部功能
auto_show_remaining_sec相关的逻辑位于：void TimerWidget::update(double delta_time_ms) 函数

widgets模块日志：
[WidgetService] 收到命令: topic=cmd.widget.timer.start payload={"action": "start", "widget_id": "timer", "mode": "countdown", "duration_sec": 12, "timeout_ms": 1000, "slot": "both", "style": {"digit_color": [255, 180, 0], "colon_blink": true}}
[WidgetService] 处理命令: widget=timer action=start
[WidgetService] 请求获取 LCD 控制权...
{"ts":"2026-03-12T15:19:09.918Z","lvl":"INFO","svc":"eye_engine","cmp":"lcd_transport_lcdcontrol","trace_id":"lcdcontrol@367674508224","msg":"LcdControlTransport initialized with buffer size 86400"}
[WidgetService] LCD 已初始化 (LCD_12BIT)
[WidgetService] LCD 控制权已获取
[WidgetService] Widget 已显示: timer timeout_ms=1000
-------------> TimerWidget::start called
{"ts":"2026-03-12T15:19:09.966Z","lvl":"INFO","svc":"timer_widget","cmp":"emitCountdownTick","trace_id":"","msg":"[TimerWidget] publish event.eye.timer.tick remaining=12"}
{"ts":"2026-03-12T15:19:10.869Z","lvl":"INFO","svc":"timer_widget","cmp":"emitCountdownTick","trace_id":"","msg":"[TimerWidget] publish event.eye.timer.tick remaining=11"}
[WidgetService] Widget 显示超时，自动隐藏
[WidgetService] 准备释放 LCD 控制权...
{"ts":"2026-03-12T15:19:10.969Z","lvl":"INFO","svc":"eye_engine","cmp":"lcd_transport_lcdcontrol","trace_id":"lcdcontrol@367674508224","msg":"LcdControlTransport shut down"}
[WidgetService] LCD 控制权已释放
[WidgetService] ✅ Widget 已完全隐藏: 

根据日志可以看到在收到命令后1秒钟后把Widget 已完全隐藏: 之后就没有任何动静了！
请分析并解决问题

