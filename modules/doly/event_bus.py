"""
Doly 事件系统

定义事件类型和事件总线，用于模块间通信。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import json
import time
import logging
import threading
from enum import Enum
from dataclasses import dataclass, field, asdict
from typing import Dict, Any, List, Callable, Optional
from queue import Queue, Empty

import zmq

logger = logging.getLogger(__name__)


class EventType(Enum):
    """事件类型枚举"""
    
    # ===== 输入事件 =====
    # 触摸事件
    TOUCH_PRESSED = "touch.pressed"
    TOUCH_RELEASED = "touch.released"
    TOUCH_HEAD = "touch.head"
    TOUCH_BODY = "touch.body"
    
    # 传感器事件
    CLIFF_DETECTED = "cliff.detected"
    CLIFF_CLEARED = "cliff.cleared"
    OBSTACLE_DETECTED = "obstacle.detected"
    OBSTACLE_CLEARED = "obstacle.cleared"
    IMU_TILT = "imu.tilt"
    IMU_SHAKE = "imu.shake"
    
    # 电量事件
    BATTERY_LOW = "battery.low"
    BATTERY_CRITICAL = "battery.critical"
    CHARGING_START = "charging.start"
    CHARGING_COMPLETE = "charging.complete"
    
    # 语音事件
    VOICE_WAKEUP = "voice.wakeup"
    VOICE_COMMAND = "voice.command"
    VOICE_ASR_RESULT = "voice.asr_result"
    
    # 视觉事件
    FACE_DETECTED = "face.detected"
    FACE_LOST = "face.lost"
    FACE_RECOGNIZED = "face.recognized"
    
    # ★★★ 新增：Vision Service 通用事件 ★★★
    VISION_EVENT = "vision.event"
    
    # ===== 系统事件 =====
    # Blockly 事件
    BLOCKLY_RECEIVED = "blockly.received"
    BLOCKLY_START = "blockly.start"
    BLOCKLY_STOP = "blockly.stop"
    BLOCKLY_COMPLETE = "blockly.complete"
    BLOCKLY_ERROR = "blockly.error"
    
    # 状态事件
    STATE_CHANGED = "state.changed"
    MODE_CHANGED = "mode.changed"
    
    # 动画事件
    ANIMATION_START = "animation.start"
    ANIMATION_COMPLETE = "animation.complete"
    ANIMATION_ERROR = "animation.error"
    
    # 系统事件
    SYSTEM_READY = "system.ready"
    SYSTEM_SHUTDOWN = "system.shutdown"
    SYSTEM_ERROR = "system.error"
    SYSTEM_EVENT = "system.event"  # ★ 通用系统事件（用于 eyeEngine overlay 等）
    
    # eyeEngine 动画事件
    OVERLAY_STARTED = "overlay.started"
    OVERLAY_COMPLETED = "overlay.completed"
    OVERLAY_STOPPED = "overlay.stopped"
    OVERLAY_FAILED = "overlay.failed"

    # Widget 事件
    WIDGET_EVENT = "widget.event"

    # ★★★ 小智事件 ★★★
    XIAOZHI_EMOTION = "xiaozhi.emotion"
    XIAOZHI_STATE = "xiaozhi.state"
    XIAOZHI_ACTION = "xiaozhi.action"
    XIAOZHI_INTENT = "xiaozhi.intent"


@dataclass
class DolyEvent:
    """Doly 事件数据结构"""
    
    type: EventType
    timestamp: float = field(default_factory=time.time)
    source: str = ""
    topic: str = "" # 新增 topic 属性
    data: Dict[str, Any] = field(default_factory=dict)
    priority: int = 5  # 1-10, 1 最高
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            'type': self.type.value if isinstance(self.type, EventType) else str(self.type),
            'timestamp': self.timestamp,
            'source': self.source,
            'topic': self.topic, # 包含 topic
            'data': self.data,
            'priority': self.priority
        }
    
    def to_json(self) -> str:
        """转换为 JSON 字符串"""
        return json.dumps(self.to_dict(), ensure_ascii=False)
    
    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> 'DolyEvent':
        """从字典创建事件"""
        event_type = d.get('type', '')
        # 尝试解析 EventType
        try:
            if isinstance(event_type, str):
                event_type = EventType(event_type)
        except ValueError:
            logger.warning(f"Unknown event type: {event_type}")
            event_type = EventType.SYSTEM_ERROR
        
        return cls(
            type=event_type,
            timestamp=d.get('timestamp', time.time()),
            source=d.get('source', ''),
            data=d.get('data', {}),
            priority=d.get('priority', 5)
        )
    
    @classmethod
    def from_json(cls, json_str: str) -> 'DolyEvent':
        """从 JSON 字符串创建事件"""
        return cls.from_dict(json.loads(json_str))


# 事件回调类型
EventCallback = Callable[[DolyEvent], None]


class EventBus:
    """
    事件总线
    
    负责：
    - 事件发布和订阅
    - 优先级队列处理
    - ZMQ 消息转发
    """
    
    def __init__(self, 
                 pub_endpoint: str = "ipc:///tmp/doly_events.sock",
                 enable_zmq: bool = True):
        """
        初始化事件总线
        
        Args:
            pub_endpoint: ZMQ PUB 端点
            enable_zmq: 是否启用 ZMQ 发布
        """
        self.pub_endpoint = pub_endpoint
        self.enable_zmq = enable_zmq
        
        # Daemon 引用（用于 WebSocket 访问）
        self.daemon = None
        
        # 事件队列
        self._event_queue: Queue = Queue()
        
        # 订阅者: event_type -> [callbacks]
        self._subscribers: Dict[EventType, List[EventCallback]] = {}
        
        # 全局订阅者（接收所有事件）
        self._global_subscribers: List[EventCallback] = []
        
        # 线程控制
        self._running = False
        self._dispatch_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        
        # ZMQ 上下文
        self._zmq_ctx: Optional[zmq.Context] = None
        self._zmq_pub: Optional[zmq.Socket] = None
        
        logger.info(f"[EventBus] 初始化完成 pub_endpoint={pub_endpoint} enable_zmq={enable_zmq}")
    
    def start(self) -> bool:
        """启动事件总线"""
        if self._running:
            logger.warning("[EventBus] 已在运行中")
            return True
        
        try:
            # 初始化 ZMQ
            if self.enable_zmq:
                self._zmq_ctx = zmq.Context()
                self._zmq_pub = self._zmq_ctx.socket(zmq.PUB)
                self._zmq_pub.bind(self.pub_endpoint)
                logger.info(f"[EventBus] ZMQ PUB 绑定到 {self.pub_endpoint}")
            
            # 启动分发线程
            self._running = True
            self._dispatch_thread = threading.Thread(
                target=self._dispatch_loop,
                name="EventBus-Dispatch",
                daemon=True
            )
            self._dispatch_thread.start()
            
            logger.info("[EventBus] 启动成功")
            return True
            
        except Exception as e:
            logger.error(f"[EventBus] 启动失败: {e}")
            self._running = False
            return False
    
    def stop(self):
        """停止事件总线"""
        logger.info("[EventBus] 停止中...")
        self._running = False
        
        # 等待分发线程结束
        if self._dispatch_thread and self._dispatch_thread.is_alive():
            self._dispatch_thread.join(timeout=2.0)
        
        # 关闭 ZMQ
        if self._zmq_pub:
            self._zmq_pub.close()
            self._zmq_pub = None
        if self._zmq_ctx:
            self._zmq_ctx.term()
            self._zmq_ctx = None
        
        logger.info("[EventBus] 已停止")
    
    def publish(self, event: DolyEvent) -> None:
        """
        发布事件
        
        Args:
            event: 要发布的事件
        """
        if not self._running:
            logger.warning(f"[EventBus] 未运行，丢弃事件: {event.type.value}")
            return
        
        logger.debug(f"[EventBus] 发布事件: type={event.type.value} source={event.source}")
        
        # 同步分发（避免队列死锁问题）
        self._dispatch_event(event)
        
        # 通过 ZMQ 发布
        if self.enable_zmq and self._zmq_pub:
            try:
                topic = event.type.value
                payload = event.to_json()
                # ★ 诊断日志：记录 ZMQ 发送
                # logger.info(f"[EventBus] 📤 ZMQ 发送: topic={topic}")
                if "cmd.vision.mode" in topic:
                    logger.info(f"[EventBus] 📤 ZMQ Vision Mode 命令: {payload[:200]}")
                self._zmq_pub.send_multipart([
                    topic.encode('utf-8'),
                    payload.encode('utf-8')
                ])
                logger.debug(f"[EventBus] ✅ ZMQ 发送成功: {topic}")
            except Exception as e:
                logger.error(f"[EventBus] ZMQ 发布失败: {e}")
    
    def subscribe(self, 
                  event_types: List[EventType], 
                  callback: EventCallback) -> None:
        """
        订阅事件
        
        Args:
            event_types: 要订阅的事件类型列表
            callback: 回调函数
        """
        with self._lock:
            for event_type in event_types:
                if event_type not in self._subscribers:
                    self._subscribers[event_type] = []
                if callback not in self._subscribers[event_type]:
                    self._subscribers[event_type].append(callback)
                    logger.info(f"[EventBus] 订阅事件: {event_type.value} -> {callback.__name__}")
    
    def subscribe_all(self, callback: EventCallback) -> None:
        """
        订阅所有事件
        
        Args:
            callback: 回调函数
        """
        with self._lock:
            if callback not in self._global_subscribers:
                self._global_subscribers.append(callback)
                logger.debug("[EventBus] 订阅所有事件")
    
    def unsubscribe(self, 
                    event_types: List[EventType], 
                    callback: EventCallback) -> None:
        """
        取消订阅
        
        Args:
            event_types: 事件类型列表
            callback: 回调函数
        """
        with self._lock:
            for event_type in event_types:
                if event_type in self._subscribers:
                    if callback in self._subscribers[event_type]:
                        self._subscribers[event_type].remove(callback)
    
    def _dispatch_loop(self):
        """事件分发循环"""
        logger.info("[EventBus] 分发线程启动")
        logger.info(f"[EventBus] _running={self._running}, 队列类型={type(self._event_queue)}")
        event_count = 0
        heartbeat = 0
        
        while True:  # 改为无条件循环先看是否线程能执行
            if not self._running:
                logger.info(f"[EventBus] _running已变为False,停止分发")
                break
                
            try:
                # 从队列获取事件（非阻塞或者短超时）
                event = self._event_queue.get(timeout=0.5)
                event_count += 1
                
                if event_count % 10 == 0 or event.type.value == 'touch.pressed':
                    logger.info(f"[EventBus] 📥 从队列取出事件 #{event_count}: {event.type.value}")
                
                # 分发到订阅者
                self._dispatch_event(event)
                
            except Empty:
                heartbeat += 1
                if heartbeat % 20 == 0:  # 每10秒打印一次
                    logger.debug(f"[EventBus] 分发线程心跳 (队列大小: {self._event_queue.qsize()})")
                continue
            except Exception as e:
                logger.error(f"[EventBus] 分发异常: {e}", exc_info=True)
        
        logger.info(f"[EventBus] 分发线程结束 (处理了{event_count}个事件)")
    
    def _dispatch_event(self, event: DolyEvent):
        """
        分发事件到订阅者
        
        Args:
            event: 事件
        """
        callbacks = []
        
        with self._lock:
            # 获取特定事件类型的订阅者
            if event.type in self._subscribers:
                callbacks.extend(self._subscribers[event.type])
                # logger.info(f"[EventBus] 分发事件 {event.type.value} 找到 {len(self._subscribers[event.type])} 个订阅者")
            else:
                logger.warning(f"[EventBus] 分发事件 {event.type.value} 无订阅者! 已注册类型: {list(self._subscribers.keys())}")
            
            # 获取全局订阅者
            callbacks.extend(self._global_subscribers)
        
        # if len(callbacks) > 0:
        #     logger.info(f"[EventBus] 分发 {event.type.value} 到 {len(callbacks)} 个回调")
        
        # 调用回调
        for callback in callbacks:
            try:
                # logger.debug(f"[EventBus] 调用回调 {callback.__name__}")
                callback(event)
            except Exception as e:
                logger.error(f"[EventBus] 回调执行失败 {callback.__name__}: {e}")


class ZMQEventSubscriber:
    """
    ZMQ 事件订阅器
    
    用于从外部 ZMQ 端点订阅事件并转发到 EventBus
    """
    
    def __init__(self, 
                 endpoint: str,
                 topics: List[str],
                 event_bus: EventBus,
                 source_name: str = "zmq"):
        """
        初始化 ZMQ 订阅器
        
        Args:
            endpoint: ZMQ 端点
            topics: 订阅的主题列表
            event_bus: 事件总线
            source_name: 事件来源名称
        """
        self.endpoint = endpoint
        self.topics = topics
        self.event_bus = event_bus
        self.source_name = source_name
        
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._zmq_ctx: Optional[zmq.Context] = None
        self._zmq_sub: Optional[zmq.Socket] = None
        
        # logger.info(f"[ZMQEventSubscriber] 初始化 endpoint={endpoint} topics={topics}")
    
    def start(self) -> bool:
        """启动订阅器"""
        if self._running:
            return True
        
        try:
            self._zmq_ctx = zmq.Context()
            self._zmq_sub = self._zmq_ctx.socket(zmq.SUB)
            self._zmq_sub.connect(self.endpoint)
            
            # logger.info(f"[ZMQEventSubscriber] 已连接到: {self.endpoint}")
            
            # 订阅主题
            for topic in self.topics:
                self._zmq_sub.setsockopt_string(zmq.SUBSCRIBE, topic)
                logger.debug(f"[ZMQEventSubscriber] 已订阅主题: {topic}")
            
            logger.info(f"[ZMQEventSubscriber] 已订阅 {len(self.topics)} 个主题")
            
            # 设置接收超时
            self._zmq_sub.setsockopt(zmq.RCVTIMEO, 100)
            
            # 增加一个小延迟以确保 Publisher 充分启动
            import time
            time.sleep(1.0)
            
            # 启动接收线程
            self._running = True
            self._thread = threading.Thread(
                target=self._receive_loop,
                name=f"ZMQSub-{self.source_name}",
                daemon=True
            )
            self._thread.start()
            
            logger.info(f"[ZMQEventSubscriber] 启动成功 endpoint={self.endpoint}")
            return True
            
        except Exception as e:
            logger.error(f"[ZMQEventSubscriber] 启动失败: {e}")
            return False
    
    def stop(self):
        """停止订阅器"""
        self._running = False
        
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        
        if self._zmq_sub:
            self._zmq_sub.close()
        if self._zmq_ctx:
            self._zmq_ctx.term()
    
    def _receive_loop(self):
        """接收循环"""
        logger.info(f"[ZMQEventSubscriber] 接收线程启动 source={self.source_name}")
        
        while self._running:
            try:
                # 接收消息
                parts = self._zmq_sub.recv_multipart()
                if len(parts) >= 2:
                    topic = parts[0].decode('utf-8')
                    payload = parts[1].decode('utf-8')
                    
                    # ★ 只记录 vision 事件
                    if 'vision' in topic.lower() and 'recognized' in topic:
                        logger.info(f"[ZMQEventSubscriber] ✅ 收到 vision.recognized 消息: topic={topic}")
                    
                    # 转换为事件
                    event = self._parse_message(topic, payload)
                    if event:
                        # ★ 只记录 vision 事件
                        if 'vision' in topic.lower() and 'recognized' in topic:
                            logger.info(f"[ZMQEventSubscriber] ✅ 转发 vision 事件: {event.type} src={self.source_name}")
                        self.event_bus.publish(event)
                        
            except zmq.Again:
                # 超时，继续
                continue
            except Exception as e:
                logger.error(f"[ZMQEventSubscriber] 接收异常: {e}")
        
        logger.info(f"[ZMQEventSubscriber] 接收线程结束 source={self.source_name}")
    
    def _parse_message(self, topic: str, payload: str) -> Optional[DolyEvent]:
        """
        解析消息为事件
        
        Args:
            topic: 主题
            payload: 负载
            
        Returns:
            DolyEvent 或 None
        """
        try:
            # 尝试解析 JSON
            data = json.loads(payload) if payload.startswith('{') else {'raw': payload}
            
            # 映射主题到事件类型（需要data辅助判断）
            event_type = self._map_topic_to_event_type(topic, data)
            
            return DolyEvent(
                type=event_type,
                source=self.source_name,
                topic=topic,
                data=data
            )
            
        except Exception as e:
            logger.error(f"[ZMQEventSubscriber] 解析消息失败: {e}")
            return None
    
    def _map_topic_to_event_type(self, topic: str, data: dict = None) -> EventType:
        """
        将 ZMQ 主题映射到事件类型
        
        Args:
            topic: ZMQ 主题
            data: 消息payload数据（用于辅助判断）
            
        Returns:
            EventType
        """
        # ★ eyeEngine overlay 事件（seq 动画生命周期）
        if topic.startswith('overlay.'):
            # 这些事件需要特殊处理，传给 animation_integration
            logger.info(f"[EventBus] 识别到 overlay 事件: {topic} - {data}")
            # 返回 SYSTEM_EVENT 作为占位，daemon 会在 handle_zmq_event 中特殊处理
            return EventType.SYSTEM_EVENT
        
        # 语音命令 (新格式: voice.command/voice.wakeup)
        if topic.startswith('voice.'):
            if topic == 'voice.wakeup':
                return EventType.VOICE_WAKEUP
            elif topic == 'voice.command':
                return EventType.VOICE_COMMAND
        
        # 语音命令 (旧格式: event.audio.*)
        if topic.startswith('event.audio.'):
            cmd_name = topic.replace('event.audio.', '')
            if cmd_name in ['wakeup_detected', 'cmd_iHelloDoly', 'cmd_iInterrupt']:
                return EventType.VOICE_WAKEUP
            return EventType.VOICE_COMMAND

        # widget_service 事件
        if topic.startswith('event.widget.') or topic.startswith('status.widget.'):
            return EventType.WIDGET_EVENT
        
        # ★★★ 小智事件 ★★★
        if topic.startswith('emotion.xiaozhi'):
            logger.debug(f"[EventBus] 识别到小智情绪事件: {data}")
            return EventType.XIAOZHI_EMOTION
        
        if topic.startswith('cmd.xiaozhi.action'):
            logger.debug(f"[EventBus] 识别到小智动作指令: {data}")
            return EventType.XIAOZHI_ACTION
        
        if topic.startswith('cmd.xiaozhi.intent'):
            logger.debug(f"[EventBus] 识别到小智意图指令: {data}")
            return EventType.XIAOZHI_INTENT
        
        # ★★★ 兼容旧格式小智情绪/状态事件 ★★★
        if topic.startswith('status.xiaozhi.'):
            if topic == 'status.xiaozhi.emotion':
                logger.debug(f"[EventBus] 识别到小智情绪事件（旧格式）: {data}")
                return EventType.XIAOZHI_EMOTION
            elif topic == 'status.xiaozhi.state':
                logger.debug(f"[EventBus] 识别到小智状态事件: {data}")
                return EventType.XIAOZHI_STATE
            return EventType.SYSTEM_EVENT
        
        # io.pca9535 事件（触摸、悬崖、按钮等）
        if topic.startswith('io.pca9535.'):
            # 高级事件：触摸手势（由drive已处理好的gesture）
            if topic == 'io.pca9535.touch.gesture':
                logger.debug(f"[EventBus] 识别到触摸手势事件: {data}")
                return EventType.TOUCH_PRESSED
            
            # 高级事件：悬崖模式（由drive已处理好的pattern）
            if topic == 'io.pca9535.cliff.pattern':
                logger.debug(f"[EventBus] 识别到悬崖模式事件: {data}")
                return EventType.CLIFF_DETECTED
            
            # 原始事件：pin状态变化（低优先级）
            if 'pin.change' in topic and data:
                pin = data.get('pin', '')
                # 悬崖传感器引脚 (IRS_FL/FR/BL/BR)
                if pin.startswith('IRS_'):
                    return EventType.CLIFF_DETECTED
                # 触摸传感器引脚 (TOUCH_L/R)
                # if pin.startswith('TOUCH_'):
                #     return EventType.TOUCH_PRESSED
                # 其他未知引脚
                # logger.debug(f"[EventBus] 未知pin类型: {pin}")
                # return EventType.SYSTEM_ERROR
            
            # raw.state等其他事件暂时忽略（不要返回 SYSTEM_ERROR，避免日志噪音）
            if 'raw.state' in topic:
                logger.debug(f"[EventBus] 忽略原始 raw.state 事件: {topic}")
                return EventType.SYSTEM_ERROR
            logger.debug(f"[EventBus] 忽略PCA9535其他事件: {topic}")
            return EventType.SYSTEM_ERROR
        
        # 传感器事件
        if topic.startswith('sensor.'):
            if 'cliff' in topic:
                return EventType.CLIFF_DETECTED
            if 'obstacle' in topic:
                return EventType.OBSTACLE_DETECTED
            if 'battery' in topic:
                return EventType.BATTERY_LOW
        
        # ★★★ Vision Service (FaceReco) 事件 ★★★
        if topic.startswith('event.vision.'):
            # logger.info(f"[EventBus] 识别到 Vision 事件: {topic}")
            return EventType.VISION_EVENT
        
        if topic.startswith('status.vision.'):
            # logger.info(f"[EventBus] 识别到 Vision 状态事件: {topic}")
            return EventType.VISION_EVENT
        
        # 默认
        return EventType.SYSTEM_ERROR


if __name__ == '__main__':
    # 简单测试
    logging.basicConfig(level=logging.DEBUG, 
                        format='[%(asctime)s] [%(levelname)s] %(message)s')
    
    # 创建事件总线
    bus = EventBus(enable_zmq=False)
    
    # 注册回调
    def on_wakeup(event: DolyEvent):
        print(f"收到唤醒事件: {event.to_dict()}")
    
    bus.subscribe([EventType.VOICE_WAKEUP], on_wakeup)
    
    # 启动
    bus.start()
    
    # 发布测试事件
    bus.publish(DolyEvent(
        type=EventType.VOICE_WAKEUP,
        source="test",
        data={'command': 'wakeup_detected'}
    ))
    
    time.sleep(1)
    bus.stop()
