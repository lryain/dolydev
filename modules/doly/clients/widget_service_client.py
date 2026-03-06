"""
WidgetService ZMQ 客户端

封装与 widget_service 的所有 ZMQ 通信

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import zmq
import json
import logging
import threading
import time
from typing import Optional, Dict, Any, Callable, List

logger = logging.getLogger(__name__)

# widget_service 使用的 PUB/SUB 端点
BUS_ENDPOINT = "ipc:///tmp/doly_bus.sock"          # 命令（PUB -> SUB）
EVENT_ENDPOINT = "ipc:///tmp/doly_widget_pub.sock"  # 事件（SUB <- PUB）
CLOCK_API_ENDPOINT = "ipc:///tmp/doly_clock_api.sock"  # Clock API (REQ/REP)


class WidgetServiceClient:
    """widget_service ZMQ 客户端（基于 BUS 的 PUB/SUB 协议）"""

    def __init__(self, cmd_endpoint: str = BUS_ENDPOINT, event_endpoint: str = EVENT_ENDPOINT,
                 api_endpoint: str = CLOCK_API_ENDPOINT):
        self.cmd_endpoint = cmd_endpoint
        self.event_endpoint = event_endpoint
        self.api_endpoint = api_endpoint
        self.ctx = zmq.Context.instance()
        self._sub_thread: Optional[threading.Thread] = None
        self._sub_running = False
        self._event_callback: Optional[Callable[[str, Dict[str, Any]], None]] = None
        logger.info(f"WidgetServiceClient 初始化: cmd={cmd_endpoint}, event={event_endpoint}, api={api_endpoint}")

    # ===== 内部工具 =====

    def _publish(self, topic: str, payload: Dict[str, Any]) -> bool:
        """通过 PUB 发送 widget 命令（与 widget_service 协议保持一致）"""
        sock = None
        try:
            sock = self.ctx.socket(zmq.PUB)
            sock.setsockopt(zmq.LINGER, 0)
            try:
                sock.bind(self.cmd_endpoint)
            except zmq.ZMQError:
                # 如果 bind 失败尝试 connect（便于本地测试）
                sock.connect(self.cmd_endpoint)
            # slow joiner 保护
            time.sleep(0.3)
            msg = json.dumps(payload)
            logger.debug(f"[CMD] topic={topic}")
            logger.debug(f"      payload={msg}")
    
            sock.send_multipart([
                topic.encode('utf-8'),
                json.dumps(payload).encode('utf-8')
            ])
            time.sleep(0.05)
            logger.debug(f"widget_service PUB => {topic} {payload}")
            return True
        except Exception as e:
            logger.error(f"widget_service 发送命令失败: topic={topic} err={e}")
            return False
        finally:
            if sock:
                sock.close(linger=0)

    # ===== 事件订阅 =====

    def subscribe_events(self, topics: Optional[List[str]] = None,
                         callback: Optional[Callable[[str, Dict[str, Any]], None]] = None,
                         timeout_ms: int = 1000) -> None:
        """订阅 widget_service 发布的事件

        Args:
            topics: 需要订阅的前缀列表，默认订阅 event.widget.
            callback: 回调函数，签名 (topic:str, data:dict)
            timeout_ms: 接收超时，方便安全退出
        """
        if self._sub_running:
            return

        self._event_callback = callback
        self._sub_running = True
        sub_topics = topics or ["event.widget.timer.event."]

        def _loop():
            sub_ctx = zmq.Context.instance()
            sub_sock = sub_ctx.socket(zmq.SUB)
            sub_sock.setsockopt(zmq.RCVTIMEO, timeout_ms)
            sub_sock.connect(self.event_endpoint)
            for t in sub_topics:
                sub_sock.setsockopt_string(zmq.SUBSCRIBE, t)
            logger.info(f"widget_service 事件订阅启动: {self.event_endpoint}, topics={sub_topics}")

            while self._sub_running:
                try:
                    topic, payload = sock.recv_multipart()
                    topic_str = topic.decode('utf-8')
                    try:
                        data = json.loads(payload)
                        print(f"  [EVENT] {topic_str}: {json.dumps(data, ensure_ascii=False)}")
                    except Exception:
                        data = {'raw': payload}
                        print(f"  [EVENT] {topic_str}: {payload[:100]}")
                    if self._event_callback:
                        try:
                            self._event_callback(topic, data)
                        except Exception as e:  # 用户回调容错
                            logger.error(f"widget_service 事件回调异常: {e}")
                except zmq.Again:
                    continue
                except Exception as e:
                    logger.error(f"widget_service 事件订阅异常: {e}")
                    break

            sub_sock.close(linger=0)
            logger.info("widget_service 事件订阅已停止")

        self._sub_thread = threading.Thread(target=_loop, daemon=True)
        self._sub_thread.start()

    def stop_subscribe(self):
        self._sub_running = False
        if self._sub_thread and self._sub_thread.is_alive():
            self._sub_thread.join(timeout=1.0)
    
    # ===== Clock Widget =====
    
    def show_clock(self, timeout: Optional[int] = None) -> bool:
        """
        显示时钟 widget
        
        Args:
            timeout: 超时时间(秒), None=使用默认
            
        Returns:
            是否成功
        """
        payload = {
            "action": "show",
            "widget_id": "clock",
            "slot": "both",
            "timeout_ms": int(timeout * 1000) if timeout else 0
        }
        return self._publish("cmd.widget.clock.show", payload)
    
    def show_date(self, timeout: Optional[int] = None) -> bool:
        """
        显示日期 widget
        
        Args:
            timeout: 超时时间(秒), None=使用默认
            
        Returns:
            是否成功
        """
        payload = {
            "action": "show",
            "widget_id": "clock",
            "slot": "both",
            "timeout_ms": int(timeout * 1000) if timeout else 0,
            # date 目前复用 clock，留出扩展字段
            "config": {"mode": "date"}
        }
        return self._publish("cmd.widget.clock.show", payload)
    
    # ===== Timer Widget =====
    
    def start_countdown(self, duration: int, auto_start: bool = False, 
                       timeout: Optional[int] = None) -> bool:
        """
        启动倒计时 widget
        
        Args:
            duration: 倒计时时长(秒)
            auto_start: 是否自动开始
            timeout: 超时时间(秒), None=使用默认
            
        Returns:
            是否成功
        """
        payload = {
            "action": "start",
            "widget_id": "timer",
            "mode": "countdown",
            "auto_hide": True,
            "timeout_ms": int(timeout * 1000) if timeout else 0,
            "duration_sec": int(duration),
            "slot": "both"
        }
        ok = self._publish("cmd.widget.timer.start", payload)
        # 如果需要非自动开始，发送一个 pause 让计时停在初始界面
        if ok and not auto_start:
            time.sleep(0.05)
            self.timer_control("pause")
        return ok
    
    def start_timer(self, auto_start: bool = False, timeout: Optional[int] = None) -> bool:
        """
        启动正计时 widget
        
        Args:
            auto_start: 是否自动开始
            timeout: 超时时间(秒), None=使用默认
            
        Returns:
            是否成功
        """
        payload = {
            "action": "start",
            "widget_id": "timer",
            "mode": "countup",
            "auto_hide": True,
            "timeout_ms": int(timeout * 1000) if timeout else 0,
            "slot": "both"
        }
        ok = self._publish("cmd.widget.timer.start", payload)
        if ok and not auto_start:
            time.sleep(0.05)
            self.timer_control("pause")
        return ok
    
    def timer_control(self, control_action: str) -> bool:
        """
        控制计时器
        
        Args:
            control_action: 控制动作(start/pause/resume/cancel)
            
        Returns:
            是否成功
        """
        control_action = control_action.lower()
        topic_map = {
            "start": "cmd.widget.timer.start",
            "pause": "cmd.widget.timer.pause",
            "resume": "cmd.widget.timer.resume",
            "cancel": "cmd.widget.timer.stop",
            "stop": "cmd.widget.timer.stop"
        }
        topic = topic_map.get(control_action)
        if not topic:
            logger.warning(f"未知计时器控制动作: {control_action}")
            return False

        payload = {"action": topic.split('.')[-1], "widget_id": "timer"}
        return self._publish(topic, payload)

    def widget_control(self, widget: str, timeout: Optional[int] = None,
                       tts: bool = False, duration: Optional[int] = None,
                       auto_start: Optional[bool] = None) -> bool:
        """
        通用 Widget 控制入口，映射到具体的命令或 bus topic。

        Args:
            widget: widget 名称 (clock/date/countdown/timer/alarm/weather)
            timeout: 超时时间（秒）
            tts: 是否播报（仅做标识）
            duration: 倒计时时长（秒）
            auto_start: 倒计时是否自动开始

        Returns:
            是否成功（尽量返回 True，当无法确认时返回 True 表示已发送）
        """
        widget = (widget or "").lower()
        print(f"---------> widget_control 请求: widget={widget}, timeout={timeout}, tts={tts}, duration={duration}, auto_start={auto_start}")
        try:
            if widget == 'clock':
                payload = {
                    'action': 'show',
                    'widget_id': 'clock',
                    'slot': 'both',
                    # 'timeout_ms': int((timeout or 5) * 1000),
                    "config": {
                        "layout": "split",
                        "hour_format": "24h",
                        "digit_color": {"r": 0, "g": 255, "b": 0},
                        "colon_blink": True
                    }
                }
                return self._publish('cmd.widget.clock.show', payload)
            elif widget == 'date':
                payload = {
                    'action': 'show',
                    'widget_id': 'clock',
                    'slot': 'both',
                    # 'timeout_ms': int((timeout or 5) * 1000),
                    'config': {'mode': 'date'}
                }
                return self._publish('cmd.widget.clock.show', payload)
            elif widget == 'countdown':
                dur = duration or 30
                payload = {
                    'action': 'start',
                    'widget_id': 'timer',
                    'mode': 'countdown',
                    'duration_sec': dur,
                    "auto_hide": True,
                    # 'timeout_ms': dur* 1000 + int((timeout or 5) * 1000),
                    'slot': 'both',
                    'style': {
                        'digit_color': [255, 180, 0],
                        'colon_blink': True
                    }
                }
                print('---------> countdown 请求: duration={duration}, timeout={timeout}, tts={tts}, auto_start={auto_start}')
                ok = self._publish('cmd.widget.timer.start', payload)
                if ok and auto_start is False:
                    time.sleep(0.05)
                    self.timer_control('pause')
                return ok
            elif widget == 'timer':
                payload = {
                    'action': 'start',
                    'widget_id': 'timer',
                    'mode': 'countup',
                    "auto_hide": True,
                    # 'timeout_ms': dur * 1000 + int((timeout or 5) * 1000),
                    'slot': 'both'
                }
                ok = self._publish('cmd.widget.timer.start', payload)
                if ok and auto_start is False:
                    time.sleep(0.05)
                    self.timer_control('pause')
                return ok
            elif widget == 'alarm':
                payload = {'action': 'show', 'widget_id': 'alarm'}
                return self._publish('cmd.widget.alarm.show', payload)
            elif widget == 'weather':
                payload = {
                    'action': 'show',
                    'widget_id': 'weather',
                    'timeout_ms': int((timeout or 10) * 1000)
                }
                return self._publish('cmd.widget.weather.show', payload)
            else:
                logger.warning(f"未知 widget 控制请求: {widget}")
                return False

        except Exception as e:
            logger.error(f"widget_control 异常: {e}")
            return False
    
    def hide_widget(self) -> bool:
        """
        隐藏当前显示的 widget
        
        Returns:
            是否成功
        """
        payload = {
            "action": "hide",
            "widget_id": "all"
        }
        return self._publish("cmd.widget.hide", payload)
    
    # ===== Alarm Widget =====
    
    def set_alarm(self, hour: int, minute: int, timeout: Optional[int] = None) -> bool:
        """
        设置闹钟 widget
        
        Args:
            hour: 小时(0-23)
            minute: 分钟(0-59)
            timeout: 超时时间(秒), None=使用默认
            
        Returns:
            是否成功
        """
        payload = {
            "action": "set",
            "widget_id": "alarm",
            "hour": hour,
            "minute": minute,
            "timeout_ms": int(timeout * 1000) if timeout else 0
        }
        return self._publish("cmd.widget.alarm.configure", payload)
    
    # ===== Weather Widget =====
    
    def show_weather(self, location: Optional[str] = None, timeout: Optional[int] = None) -> bool:
        """
        显示天气 widget
        
        Args:
            location: 位置(可选)
            timeout: 超时时间(秒), None=使用默认
            
        Returns:
            是否成功
        """
        payload = {
            "action": "show",
            "widget_id": "weather",
            "timeout_ms": int(timeout * 1000) if timeout else 0
        }
        if location:
            payload["location"] = location
        return self._publish("cmd.widget.weather.show", payload)
    
    # ===== Clock API (REQ/REP 模式) =====
    
    def _send_api_request(self, payload: Dict[str, Any], timeout_ms: int = 1000) -> Optional[Dict[str, Any]]:
        """
        通过 REQ/REP 发送 API 请求到 widget_service 的 clock API
        
        Args:
            payload: 请求数据
            timeout_ms: 超时时间(毫秒)
            
        Returns:
            响应字典，失败返回 None
        """
        try:
            sock = self.ctx.socket(zmq.REQ)
            sock.setsockopt(zmq.RCVTIMEO, timeout_ms)
            sock.setsockopt(zmq.SNDTIMEO, timeout_ms)
            sock.setsockopt(zmq.LINGER, 0)
            sock.connect(self.api_endpoint)
            
            sock.send_json(payload)
            resp = sock.recv_json()
            sock.close(linger=0)
            return resp
        except zmq.Again:
            logger.debug(f"Clock API 请求超时: {payload.get('action')}")
            return None
        except Exception as e:
            logger.error(f"Clock API 请求异常: {e}")
            return None
    
    def get_time(self) -> Optional[Dict[str, Any]]:
        """
        查询当前时间 (通过 Clock API)
        
        Returns:
            时间字典，例如: {"hour": 14, "minute": 30, "second": 45}
        """
        resp = self._send_api_request({"action": "get_time"})
        return resp if resp and not resp.get('error') else None
    
    def chime_now(self, language: str = "zh") -> bool:
        """
        触发整点报时 (通过 Clock API)
        
        Args:
            language: 语言代码 (zh/en)
            
        Returns:
            是否成功
        """
        payload = {"action": "chime_now"}
        if language:
            payload["language"] = "zh" if language == "cn" else language
        resp = self._send_api_request(payload)
        return resp is not None and not resp.get('error')
    
    def announce_time(self, language: str = "zh") -> bool:
        """
        语音报时 (通过 Clock API)
        
        Args:
            language: 语言代码 (zh/en)
            
        Returns:
            是否成功
        """
        payload = {"action": "announce_time"}
        if language:
            payload["language"] = "zh" if language == "cn" else language
        resp = self._send_api_request(payload)
        return resp is not None and not resp.get('error')
    
    # ===== Status =====
    
    def get_status(self) -> Optional[Dict[str, Any]]:
        """获取 widget_service 状态"""
        # widget_service 当前未提供 REQ/REP 状态查询，这里通过事件订阅实现
        status_holder = {}
        done = threading.Event()

        def _on_event(topic: str, data: Dict[str, Any]):
            print(f"---------> get_status 事件回调: topic={topic}, data={data}")
            if topic == "status.widget.state":
                status_holder.update(data)
                done.set()

        self.subscribe_events(["status.widget."], _on_event, timeout_ms=500)
        done.wait(timeout=1.0)
        self.stop_subscribe()
        return status_holder or None
    
    def ping(self) -> bool:
        """Ping widget_service"""
        # 简单地尝试订阅一次事件判断服务是否存活
        pong = False

        def _cb(topic: str, data: Dict[str, Any]):
            nonlocal pong
            pong = True

        self.subscribe_events(["status.widget."], _cb, timeout_ms=200)
        time.sleep(0.25)
        self.stop_subscribe()
        return pong
    
    # ===== Cleanup =====
    
    def close(self):
        """关闭连接"""
        self.stop_subscribe()
        logger.info("WidgetServiceClient 已关闭")
