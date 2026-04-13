# FaceReco 视觉服务 - Daemon 集成设计

> **版本**: 1.0  
> **日期**: 2026-02-08  

---

## 1. 概述

本文档描述如何在 Doly Daemon 中集成人脸识别功能，通过 `FaceRecoManager` 管理器实现人脸事件的接收、处理和行为触发。

---

## 2. FaceRecoManager 设计

### 2.1 类结构

```python
# modules/doly/managers/face_reco_manager.py

from typing import Optional, Dict, Any, Callable, List
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
import time
import logging
import yaml

logger = logging.getLogger(__name__)


class FaceRelation(Enum):
    """人脸关系类型"""
    MASTER = "master"       # 主人
    FAMILY = "family"       # 家人
    FRIEND = "friend"       # 朋友
    ACQUAINTANCE = "acquaintance"  # 熟人
    STRANGER = "stranger"   # 陌生人
    UNKNOWN = "unknown"     # 未知


@dataclass
class FaceProfile:
    """人脸档案"""
    face_id: str
    name: str
    relation: FaceRelation = FaceRelation.UNKNOWN
    last_seen_ms: int = 0
    last_greeted_ms: int = 0
    greeting_count: int = 0
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class TrackedFace:
    """当前跟踪的人脸"""
    tracker_id: int
    face_id: Optional[str] = None
    name: str = "unknown"
    relation: FaceRelation = FaceRelation.UNKNOWN
    bbox: Dict[str, float] = field(default_factory=dict)
    normalized: Dict[str, float] = field(default_factory=dict)
    confidence: float = 0.0
    liveness: bool = False
    first_seen_ms: int = 0
    last_seen_ms: int = 0
    recognized: bool = False
    greeted: bool = False


class FaceRecoManager:
    """
    人脸识别事件管理器
    
    职责：
    - 订阅 Vision Service 的人脸事件
    - 维护当前跟踪的人脸列表
    - 根据配置触发行为（动画、语音、动作）
    - 管理人脸注册流程
    - 实现眼神跟随逻辑
    """
    
    def __init__(self, config_path: Optional[str] = None):
        self.config_path = Path(config_path or 
            "/home/pi/dolydev/libsconfig/face_reco_settings.yaml")
        
        # 配置
        self.config: Dict[str, Any] = {}
        self.enabled: bool = True
        
        # 人脸数据
        self.known_faces: Dict[str, FaceProfile] = {}
        self.current_faces: Dict[int, TrackedFace] = {}
        self.primary_face_id: Optional[int] = None
        
        # 状态
        self.last_greet_time: Dict[str, float] = {}  # face_id -> 上次打招呼时间
        self.service_connected: bool = False
        
        # 外部依赖（由 daemon 设置）
        self.animation_manager = None
        self.tts_client = None
        self.eye_client = None
        self.state_provider: Optional[Callable[[], str]] = None
        self.zmq_publisher = None
        
        # 回调
        self.on_face_recognized: Optional[Callable[[TrackedFace], None]] = None
        self.on_face_lost: Optional[Callable[[TrackedFace], None]] = None
        self.on_new_face: Optional[Callable[[TrackedFace], None]] = None
        
        # 加载配置
        self._load_config()
        
        logger.info("✅ [FaceRecoManager] 初始化完成")
    
    def _load_config(self) -> None:
        """加载配置文件"""
        try:
            if self.config_path.exists():
                with open(self.config_path, 'r', encoding='utf-8') as f:
                    self.config = yaml.safe_load(f) or {}
                    
                settings = self.config.get('face_recognition', {})
                self.enabled = settings.get('enabled', True)
                self.auto_greet = settings.get('auto_greet', True)
                self.greet_cooldown = settings.get('greet_cooldown_seconds', 300)
                self.recognition_threshold = settings.get('recognition_threshold', 0.85)
                self.liveness_required = settings.get('liveness_required', True)
                
                self.new_face_config = settings.get('new_face_behavior', {})
                self.known_face_behaviors = settings.get('known_face_behaviors', {})
                self.face_lost_config = settings.get('face_lost_behaviors', {})
                self.gaze_config = settings.get('gaze_follow', {})
                
                logger.info(f"[FaceRecoManager] 配置加载完成")
            else:
                logger.warning(f"[FaceRecoManager] 配置文件不存在: {self.config_path}")
                self._use_default_config()
        except Exception as e:
            logger.error(f"[FaceRecoManager] 加载配置失败: {e}")
            self._use_default_config()
    
    def _use_default_config(self) -> None:
        """使用默认配置"""
        self.enabled = True
        self.auto_greet = True
        self.greet_cooldown = 300
        self.recognition_threshold = 0.85
        self.liveness_required = True
        self.new_face_config = {'enabled': True, 'prompt_registration': True}
        self.known_face_behaviors = {}
        self.face_lost_config = {'delay_seconds': 2.0}
        self.gaze_config = {'enabled': True, 'smoothing': 0.15}
    
    # ========== 外部依赖设置 ==========
    
    def set_animation_manager(self, manager) -> None:
        """设置动画管理器"""
        self.animation_manager = manager
        
    def set_tts_client(self, client) -> None:
        """设置 TTS 客户端"""
        self.tts_client = client
        
    def set_eye_client(self, client) -> None:
        """设置眼睛引擎客户端"""
        self.eye_client = client
        
    def set_state_provider(self, provider: Callable[[], str]) -> None:
        """设置状态提供器"""
        self.state_provider = provider
        
    def set_zmq_publisher(self, publisher) -> None:
        """设置 ZMQ 发布器（用于发送命令到 Vision Service）"""
        self.zmq_publisher = publisher
    
    # ========== 事件处理 ==========
    
    def handle_event(self, event: Dict[str, Any]) -> bool:
        """
        处理视觉服务事件
        
        Args:
            event: 事件数据
            
        Returns:
            是否成功处理
        """
        if not self.enabled:
            return False
            
        event_type = event.get('event', '')
        
        if event_type == 'face_snapshot':
            return self._handle_face_snapshot(event)
        elif event_type == 'face_recognized':
            return self._handle_face_recognized(event)
        elif event_type == 'face_new':
            return self._handle_new_face(event)
        elif event_type == 'face_lost':
            return self._handle_face_lost(event)
        else:
            logger.debug(f"[FaceRecoManager] 未处理的事件类型: {event_type}")
            return False
    
    def _handle_face_snapshot(self, event: Dict[str, Any]) -> bool:
        """处理人脸快照事件"""
        faces = event.get('faces', [])
        primary = event.get('primary', {})
        
        # 更新当前跟踪的人脸
        seen_ids = set()
        for face_data in faces:
            tracker_id = face_data.get('id')
            if tracker_id is None:
                continue
                
            seen_ids.add(tracker_id)
            
            if tracker_id not in self.current_faces:
                # 新的跟踪目标
                self.current_faces[tracker_id] = TrackedFace(
                    tracker_id=tracker_id,
                    first_seen_ms=int(time.time() * 1000)
                )
            
            # 更新位置信息
            face = self.current_faces[tracker_id]
            face.bbox = face_data.get('bbox', {})
            face.normalized = face_data.get('normalized', {})
            face.last_seen_ms = int(time.time() * 1000)
            face.confidence = face_data.get('confidence', 0.0)
            face.liveness = face_data.get('liveness', False)
            if face_data.get('name') and face_data['name'] != 'unknown':
                face.name = face_data['name']
                face.recognized = True
        
        # 更新主要人脸
        if primary:
            self.primary_face_id = primary.get('id')
            
            # 眼神跟随
            if self.gaze_config.get('enabled', True):
                self._update_gaze_follow(primary)
        
        return True
    
    def _handle_face_recognized(self, event: Dict[str, Any]) -> bool:
        """处理人脸识别成功事件"""
        tracker_id = event.get('tracker_id')
        face_id = event.get('face_id', '')
        name = event.get('name', 'unknown')
        confidence = event.get('confidence', 0.0)
        liveness = event.get('liveness', False)
        metadata = event.get('metadata', {})
        
        # 活体检测检查
        if self.liveness_required and not liveness:
            logger.info(f"[FaceRecoManager] 忽略非活体人脸: {name}")
            return False
        
        # 置信度检查
        if confidence < self.recognition_threshold:
            logger.debug(f"[FaceRecoManager] 置信度不足: {name} ({confidence:.2f})")
            return False
        
        # 更新跟踪数据
        if tracker_id in self.current_faces:
            face = self.current_faces[tracker_id]
            face.face_id = face_id
            face.name = name
            face.confidence = confidence
            face.liveness = liveness
            face.recognized = True
            
            # 确定关系类型
            relation_str = metadata.get('relation', 'unknown')
            try:
                face.relation = FaceRelation(relation_str)
            except ValueError:
                face.relation = FaceRelation.UNKNOWN
        else:
            # 创建新的跟踪记录
            face = TrackedFace(
                tracker_id=tracker_id,
                face_id=face_id,
                name=name,
                confidence=confidence,
                liveness=liveness,
                recognized=True,
                first_seen_ms=int(time.time() * 1000),
                last_seen_ms=int(time.time() * 1000)
            )
            self.current_faces[tracker_id] = face
        
        # 触发打招呼
        if self.auto_greet and not face.greeted:
            self._trigger_greeting(face)
        
        # 回调
        if self.on_face_recognized:
            self.on_face_recognized(face)
        
        logger.info(f"[FaceRecoManager] 识别到: {name} (置信度={confidence:.2f})")
        return True
    
    def _handle_new_face(self, event: Dict[str, Any]) -> bool:
        """处理新人脸出现事件"""
        tracker_id = event.get('tracker_id')
        detection_count = event.get('detection_count', 0)
        liveness = event.get('liveness', False)
        
        if not self.new_face_config.get('enabled', True):
            return False
        
        # 活体检测检查
        if self.liveness_required and not liveness:
            return False
        
        # 创建或更新跟踪记录
        if tracker_id not in self.current_faces:
            self.current_faces[tracker_id] = TrackedFace(
                tracker_id=tracker_id,
                first_seen_ms=int(time.time() * 1000),
                last_seen_ms=int(time.time() * 1000)
            )
        
        face = self.current_faces[tracker_id]
        face.liveness = liveness
        face.bbox = event.get('bbox', {})
        
        # 触发新人脸行为
        if self.new_face_config.get('prompt_registration', True):
            self._trigger_new_face_behavior(face)
        
        # 回调
        if self.on_new_face:
            self.on_new_face(face)
        
        logger.info(f"[FaceRecoManager] 新人脸出现: tracker_id={tracker_id}")
        return True
    
    def _handle_face_lost(self, event: Dict[str, Any]) -> bool:
        """处理人脸消失事件"""
        tracker_id = event.get('tracker_id')
        
        if tracker_id not in self.current_faces:
            return False
        
        face = self.current_faces[tracker_id]
        
        # 触发消失行为
        if face.recognized:
            self._trigger_face_lost_behavior(face)
        
        # 回调
        if self.on_face_lost:
            self.on_face_lost(face)
        
        # 移除跟踪记录
        del self.current_faces[tracker_id]
        
        # 更新主要人脸
        if self.primary_face_id == tracker_id:
            self.primary_face_id = None
            if self.current_faces:
                self.primary_face_id = next(iter(self.current_faces.keys()))
        
        logger.info(f"[FaceRecoManager] 人脸消失: {face.name} (tracker_id={tracker_id})")
        return True
    
    # ========== 行为触发 ==========
    
    def _trigger_greeting(self, face: TrackedFace) -> None:
        """触发打招呼行为"""
        # 冷却时间检查
        now = time.time()
        if face.face_id in self.last_greet_time:
            elapsed = now - self.last_greet_time[face.face_id]
            if elapsed < self.greet_cooldown:
                logger.debug(f"[FaceRecoManager] 打招呼冷却中: {face.name}")
                return
        
        # 获取行为配置
        relation_key = face.relation.value
        behavior = self.known_face_behaviors.get(relation_key, {})
        
        # 播放动画
        animation = behavior.get('greeting_animation')
        if animation and self.animation_manager:
            self.animation_manager.play_animation(animation, priority=7)
        
        # TTS 问候语
        tts_template = behavior.get('greeting_tts')
        if tts_template and self.tts_client:
            greeting_text = tts_template.format(name=face.name)
            # TODO: 调用 TTS 客户端
            logger.info(f"[FaceRecoManager] TTS: {greeting_text}")
        
        # 更新状态
        face.greeted = True
        self.last_greet_time[face.face_id] = now
        
        logger.info(f"[FaceRecoManager] 打招呼: {face.name} ({relation_key})")
    
    def _trigger_new_face_behavior(self, face: TrackedFace) -> None:
        """触发新人脸行为"""
        # 播放好奇动画
        if self.animation_manager:
            self.animation_manager.play_animation("CURIOUS.curious_look", priority=6)
        
        logger.debug(f"[FaceRecoManager] 触发新人脸行为: tracker_id={face.tracker_id}")
    
    def _trigger_face_lost_behavior(self, face: TrackedFace) -> None:
        """触发人脸消失行为"""
        delay = self.face_lost_config.get('delay_seconds', 2.0)
        animation = self.face_lost_config.get('animation')
        
        if animation and self.animation_manager:
            # 延迟执行（可考虑使用定时器）
            import threading
            def delayed_animation():
                time.sleep(delay)
                if face.tracker_id not in self.current_faces:  # 确认仍然不在
                    self.animation_manager.play_animation(animation, priority=5)
            threading.Thread(target=delayed_animation, daemon=True).start()
    
    def _update_gaze_follow(self, primary_face: Dict[str, Any]) -> None:
        """更新眼神跟随"""
        if not self.eye_client:
            return
            
        normalized = primary_face.get('normalized', {})
        x = normalized.get('x', 0.5)
        y = normalized.get('y', 0.5)
        
        # 发送眼神跟随命令
        try:
            self.eye_client.send_gaze_command(x, y)
        except Exception as e:
            logger.debug(f"[FaceRecoManager] 眼神跟随失败: {e}")
    
    # ========== 人脸管理接口 ==========
    
    def register_face(self, tracker_id: int, name: str, 
                      relation: str = "friend", metadata: Dict = None) -> bool:
        """
        注册当前检测到的人脸
        
        Args:
            tracker_id: 跟踪 ID
            name: 人脸名称
            relation: 关系类型
            metadata: 元数据
            
        Returns:
            是否成功
        """
        if not self.zmq_publisher:
            logger.warning("[FaceRecoManager] ZMQ 发布器未设置")
            return False
        
        cmd = {
            "method": "current",
            "tracker_id": tracker_id,
            "name": name,
            "metadata": {
                "relation": relation,
                **(metadata or {})
            }
        }
        
        try:
            self.zmq_publisher.send("cmd.vision.face.register", cmd)
            logger.info(f"[FaceRecoManager] 发送注册命令: {name}")
            return True
        except Exception as e:
            logger.error(f"[FaceRecoManager] 注册命令发送失败: {e}")
            return False
    
    def get_current_faces(self) -> List[Dict[str, Any]]:
        """获取当前跟踪的人脸列表"""
        return [
            {
                "tracker_id": face.tracker_id,
                "name": face.name,
                "recognized": face.recognized,
                "confidence": face.confidence,
                "relation": face.relation.value,
                "bbox": face.bbox,
                "normalized": face.normalized
            }
            for face in self.current_faces.values()
        ]
    
    def get_primary_face(self) -> Optional[Dict[str, Any]]:
        """获取主要（最大/最近）人脸"""
        if self.primary_face_id and self.primary_face_id in self.current_faces:
            face = self.current_faces[self.primary_face_id]
            return {
                "tracker_id": face.tracker_id,
                "name": face.name,
                "recognized": face.recognized,
                "normalized": face.normalized
            }
        return None
    
    # ========== 生命周期 ==========
    
    def reload_config(self) -> bool:
        """重新加载配置"""
        self._load_config()
        return True
    
    def clear_tracking(self) -> None:
        """清空当前跟踪数据"""
        self.current_faces.clear()
        self.primary_face_id = None
        
    def enable(self, enabled: bool = True) -> None:
        """启用/禁用管理器"""
        self.enabled = enabled
        logger.info(f"[FaceRecoManager] {'启用' if enabled else '禁用'}")
```

---

## 3. Daemon 集成

### 3.1 在 daemon.py 中初始化

```python
# modules/doly/daemon.py

# 在导入区域添加
from modules.doly.managers.face_reco_manager import FaceRecoManager

class DolyDaemon:
    def __init__(self, config_dir: Optional[str] = None):
        # ... 现有初始化代码 ...
        
        # ★★★ 新增：人脸识别管理器 ★★★
        self.face_reco_manager = FaceRecoManager(
            str(self.config_dir / "face_reco_settings.yaml")
        )
        
        # 设置依赖
        self.face_reco_manager.set_animation_manager(self.animation_manager)
        self.face_reco_manager.set_eye_client(self.eye_client)
        self.face_reco_manager.set_state_provider(lambda: self.state_machine.current_state)
        
        # 设置回调
        self.face_reco_manager.on_face_recognized = self._on_face_recognized
        self.face_reco_manager.on_face_lost = self._on_face_lost
        
        # ★★★ 视觉服务事件订阅器 ★★★
        self._vision_subscriber: Optional[ZMQEventSubscriber] = None
```

### 3.2 订阅视觉服务事件

```python
# modules/doly/daemon.py

def _init_vision_subscriber(self) -> None:
    """初始化视觉服务事件订阅"""
    try:
        self._vision_subscriber = ZMQEventSubscriber(
            endpoint="ipc:///tmp/doly_bus.sock",
            topics=["event.vision.face", "status.vision.state"]
        )
        self._vision_subscriber.set_callback(self._on_vision_event)
        self._vision_subscriber.start()
        logger.info("✅ 视觉服务事件订阅器已启动")
    except Exception as e:
        logger.error(f"❌ 视觉服务订阅器启动失败: {e}")

def _on_vision_event(self, topic: str, data: Dict[str, Any]) -> None:
    """处理视觉服务事件"""
    try:
        if topic.startswith("event.vision.face"):
            self.face_reco_manager.handle_event(data)
        elif topic == "status.vision.state":
            self._update_vision_status(data)
    except Exception as e:
        logger.error(f"[Daemon] 处理视觉事件失败: {e}")
```

### 3.3 回调处理

```python
# modules/doly/daemon.py

def _on_face_recognized(self, face: TrackedFace) -> None:
    """人脸识别成功回调"""
    logger.info(f"[Daemon] 人脸识别回调: {face.name}")
    
    # 更新交互时间
    self.last_interaction_time = time.time()
    
    # 如果是主人且当前处于睡眠状态，唤醒
    if face.relation.value == "master" and self.state_machine.current_state == DolyState.SLEEPING:
        self.state_machine.transition_to(DolyState.IDLE)

def _on_face_lost(self, face: TrackedFace) -> None:
    """人脸消失回调"""
    logger.debug(f"[Daemon] 人脸消失回调: {face.name}")
```

---

## 4. 配置文件

### 4.1 config/face_reco_settings.yaml

```yaml
# 人脸识别设置配置文件

face_recognition:
  # 基础设置
  enabled: true
  auto_greet: true
  greet_cooldown_seconds: 300  # 5分钟内不重复打招呼
  
  # 识别阈值
  recognition_threshold: 0.85
  liveness_required: true
  
  # 新人脸处理
  new_face_behavior:
    enabled: true
    prompt_registration: true
    auto_register_delay_seconds: 10
    # 新人脸动画
    animation: "CURIOUS.curious_look"
    
  # 已知人脸行为映射
  known_face_behaviors:
    master:
      greeting_animation: "GREETING_HAPPY.happy_master"
      greeting_tts: "主人，你回来啦！我好想你！"
      follow_gaze: true
      led_effect: "rainbow_wave"
      
    family:
      greeting_animation: "GREETING.greeting_family"
      greeting_tts: "你好呀，{name}！"
      follow_gaze: true
      led_effect: "warm_pulse"
      
    friend:
      greeting_animation: "GREETING.greeting_normal"
      greeting_tts: "嗨，{name}，好久不见！"
      follow_gaze: true
      led_effect: null
      
    acquaintance:
      greeting_animation: "GREETING.greeting_polite"
      greeting_tts: "你好，{name}。"
      follow_gaze: true
      led_effect: null
      
    stranger:
      greeting_animation: "CURIOUS.curious_look"
      greeting_tts: null
      follow_gaze: true
      led_effect: null
      
  # 人脸消失行为
  face_lost_behaviors:
    delay_seconds: 2.0
    animation: "SAD.sad_goodbye"
    tts: null
    
  # 眼神跟随设置
  gaze_follow:
    enabled: true
    smoothing: 0.15
    update_interval_ms: 50
    dead_zone: 0.05  # 移动小于此值不更新
    
  # 状态过滤：在这些状态下不处理人脸事件
  ignore_in_states:
    - "sleeping"
    - "shutdown"
```

---

## 5. 人脸数据管理

### 5.1 数据存储结构

```
libs/FaceReco/
├── img/                    # 人脸图片
│   ├── zhangsan_1738972800_0.jpg
│   ├── zhangsan_1738972801_1.jpg
│   └── lisi_1738972900_0.jpg
├── descriptors.yml         # 人脸特征描述符缓存
└── face_profiles.yaml      # 🆕 人脸档案（元数据）
```

### 5.2 face_profiles.yaml 格式

```yaml
# 人脸档案数据库

profiles:
  - face_id: "zhangsan"
    name: "张三"
    relation: "friend"
    created_at: "2026-02-01T10:00:00Z"
    last_seen: "2026-02-08T09:30:00Z"
    interaction_count: 25
    sample_images:
      - "zhangsan_1738972800_0.jpg"
      - "zhangsan_1738972801_1.jpg"
    metadata:
      birthday: "1990-05-15"
      notes: "老朋友"
      
  - face_id: "lisi"
    name: "李四"
    relation: "family"
    created_at: "2026-01-15T08:00:00Z"
    last_seen: "2026-02-08T08:00:00Z"
    interaction_count: 100
    sample_images:
      - "lisi_1738972900_0.jpg"
    metadata:
      relation_detail: "弟弟"
```

---

## 6. 测试脚本

### 6.1 test_face_reco_manager.py

```python
#!/usr/bin/env python3
"""
FaceRecoManager 测试脚本
"""

import sys
import time
sys.path.insert(0, "/home/pi/dev/nora-xiaozhi-dev")

from modules.doly.managers.face_reco_manager import (
    FaceRecoManager, FaceRelation, TrackedFace
)

def test_basic():
    """基础功能测试"""
    print("=" * 50)
    print("测试 1: 基础初始化")
    print("=" * 50)
    
    manager = FaceRecoManager()
    print(f"✅ 初始化成功")
    print(f"   enabled: {manager.enabled}")
    print(f"   auto_greet: {manager.auto_greet}")
    print(f"   greet_cooldown: {manager.greet_cooldown}")
    
def test_face_events():
    """人脸事件测试"""
    print("\n" + "=" * 50)
    print("测试 2: 人脸事件处理")
    print("=" * 50)
    
    manager = FaceRecoManager()
    
    # 模拟人脸检测事件
    snapshot_event = {
        "event": "face_snapshot",
        "count": 1,
        "primary": {
            "id": 1,
            "bbox": {"x1": 100, "y1": 80, "x2": 200, "y2": 180},
            "normalized": {"x": 0.5, "y": 0.5},
            "confidence": 0.92,
            "name": "unknown",
            "liveness": True
        },
        "faces": [{
            "id": 1,
            "bbox": {"x1": 100, "y1": 80, "x2": 200, "y2": 180},
            "normalized": {"x": 0.5, "y": 0.5}
        }]
    }
    
    result = manager.handle_event(snapshot_event)
    print(f"✅ 处理人脸快照事件: {result}")
    print(f"   当前跟踪人脸数: {len(manager.current_faces)}")
    
    # 模拟人脸识别事件
    recognized_event = {
        "event": "face_recognized",
        "tracker_id": 1,
        "face_id": "zhangsan",
        "name": "张三",
        "confidence": 0.95,
        "liveness": True,
        "metadata": {"relation": "friend"}
    }
    
    result = manager.handle_event(recognized_event)
    print(f"✅ 处理人脸识别事件: {result}")
    
    faces = manager.get_current_faces()
    print(f"   当前人脸: {faces}")
    
    # 模拟人脸消失事件
    lost_event = {
        "event": "face_lost",
        "tracker_id": 1
    }
    
    result = manager.handle_event(lost_event)
    print(f"✅ 处理人脸消失事件: {result}")
    print(f"   当前跟踪人脸数: {len(manager.current_faces)}")

def test_greeting():
    """打招呼行为测试"""
    print("\n" + "=" * 50)
    print("测试 3: 打招呼行为")
    print("=" * 50)
    
    manager = FaceRecoManager()
    
    # 记录是否触发了打招呼
    greeted = []
    def on_recognized(face):
        if face.greeted:
            greeted.append(face.name)
    
    manager.on_face_recognized = on_recognized
    
    # 模拟识别事件
    event = {
        "event": "face_recognized",
        "tracker_id": 1,
        "face_id": "master",
        "name": "主人",
        "confidence": 0.98,
        "liveness": True,
        "metadata": {"relation": "master"}
    }
    
    manager.handle_event(event)
    print(f"✅ 触发打招呼: {greeted}")

if __name__ == "__main__":
    test_basic()
    test_face_events()
    test_greeting()
    print("\n✅ 所有测试通过!")
```

---
