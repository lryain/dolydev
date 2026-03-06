"""
眼睛控制器

提供程序化眼睛控制 API

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
import threading
import time
import random
import hashlib
from typing import Optional, Dict, Callable

from .config import EyeState, EngineConfig, LcdSide, EXPRESSIONS
from .renderer import EyeRenderer
from .drivers.interfaces import ILcdDriver
from .constants import BLINK_DURATION, BLINK_FRAMES
from .config_loader import get_config_loader

logger = logging.getLogger(__name__)


class EyeController:
    """
    眼睛控制器
    
    管理眼睛状态、动画和自动眨眼逻辑
    """
    
    def __init__(self, left_driver: ILcdDriver, right_driver: ILcdDriver, 
                 renderer: EyeRenderer, config: Optional[EngineConfig] = None,
                 overlay_provider: Optional[Callable[[], list]] = None):
        """
        初始化控制器
        
        Args:
            left_driver: 左眼 LCD 驱动
            right_driver: 右眼 LCD 驱动
            renderer: 渲染器
            config: 引擎配置
        """
        self._left_driver = left_driver
        self._right_driver = right_driver
        self._renderer = renderer
        self._config = config or EngineConfig()
        # overlay_provider: callable that returns list of overlay info dicts
        self._overlay_provider = overlay_provider
        
        # 状态
        self._left_state = EyeState()
        self._right_state = EyeState()
        # 当前是否应抑制 overlay（如 blink 期间）
        self._suppress_overlays = False
        
        # 线程安全
        self._render_lock = threading.Lock()
        
        # 自动眨眼
        self._auto_blink_enabled = False
        self._blink_thread = None
        self._blink_stop_event = threading.Event()
        self._blink_interval = self._config.blink_interval
        self._blink_animations = self._config.blink_animations
        self._blink_callback: Optional[Callable[[str], None]] = None
        
        # 动画状态 (用于阻止自动眨眼和直接写屏)
        self._animating = False
        
        # 状态变化回调列表 (用于通知 Engine 同步状态)
        self._state_listeners = []
        
        # 默认背景配置 (用于超时恢复)
        self._default_bg_type = 'COLOR'
        self._default_bg_style = 'COLOR_WHITE'

    def set_default_background(self, bg_type: str, bg_style: str) -> None:
        """设置默认背景（用于超时恢复）"""
        self._default_bg_type = bg_type
        self._default_bg_style = bg_style
        logger.info(f"[Controller] 设置默认背景: type={bg_type}, style={bg_style}")

    def add_state_listener(self, callback):
        """添加一个当状态（主题/样式/眼睑等）发生变化时调用的回调函数"""
        if callback and callback not in self._state_listeners:
            self._state_listeners.append(callback)

    def _notify_state_listeners(self):
        for cb in list(self._state_listeners):
            try:
                cb()
            except Exception:
                pass

    def set_state(self, state: EyeState, side: LcdSide = LcdSide.BOTH) -> None:
        """
        设置眼睛状态
        
        Args:
            state: 新状态
            side: 目标眼睛
        """
        with self._render_lock:
            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                self._left_state = state.copy()
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                self._right_state = state.copy()
        
        # 如果正在播放动画，避免直接由 Controller 写屏（由动画播放器负责渲染），
        # 但仍然通知监听器（例如同步动画播放器的内部状态）
        if not self._animating:
            self._update_display(side)
        # 通知监听器（例如 Engine 用于同步动画播放器状态）
        self._notify_state_listeners()
    
    def get_state(self, side: LcdSide = LcdSide.LEFT) -> EyeState:
        """
        获取眼睛状态
        
        Args:
            side: 目标眼睛
            
        Returns:
            当前状态
        """
        if side == LcdSide.RIGHT:
            return self._right_state.copy()
        return self._left_state.copy()
    
    def set_iris_position(self, x: float, y: float, 
                          side: LcdSide = LcdSide.BOTH) -> None:
        """
        设置虹膜位置 (兼容归一化坐标 -1.0 ~ 1.0)
        
        Args:
            x: X 偏移 (-1.0 ~ 1.0)
            y: Y 偏移 (-1.0 ~ 1.0)
            side: 目标眼睛
        """
        # 转换归一化坐标到像素坐标 (中心 120, 范围 +/- 96)
        pixel_x = int(120 + (max(-1.0, min(1.0, x)) * 96))
        pixel_y = int(120 + (max(-1.0, min(1.0, y)) * 96))
        
        with self._render_lock:
            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                self._left_state.iris_x = pixel_x
                self._left_state.iris_y = pixel_y
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                self._right_state.iris_x = pixel_x
                self._right_state.iris_y = pixel_y
        
        if not self._animating:
            self._update_display(side)
        self._notify_state_listeners()
    
    def set_iris_theme(self, theme: str, style: str,
                       side: LcdSide = LcdSide.BOTH) -> None:
        """
        设置虹膜主题和样式
        
        Args:
            theme: 主题名称 (CLASSIC, MODERN, etc.)
            style: 样式名称 (COLOR_BLUE, APPLE, etc.)
            side: 目标眼睛
        """
        # 规范化主题为大写，便于在配置中查找
        if isinstance(theme, str):
            theme = theme.upper()

        # 兼容性转换
        if theme == "CLASSIC": theme = "classic"
        # 规范化颜色/样式（接收 color_red, COLOR_RED, red 等）
        if isinstance(style, str) and style.lower().startswith("color_"):
            style = style.upper()

        # 尝试在主题的可用样式中规范化短名称（例如 'red' -> 'COLOR_RED'）
        try:
            available = self._renderer._asset_manager.get_available_iris_styles(theme)
        except Exception:
            available = []

        if isinstance(style, str):
            s_up = style.upper()
            if s_up in available:
                style = s_up
            elif ("COLOR_" + s_up) in available:
                style = "COLOR_" + s_up
            else:
                for s in available:
                    if s.lower() == style.lower() or s.lower() == f"color_{style.lower()}" or s.lower() == style.lower().replace('color_', ''):
                        style = s
                        break
        
        with self._render_lock:
            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                self._left_state.iris_theme = theme
                self._left_state.iris_style = style
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                self._right_state.iris_theme = theme
                self._right_state.iris_style = style
        
        if not self._animating:
            self._update_display(side)
        # 通知监听器，便于外部（如 Engine）同步动画播放器状态
        self._notify_state_listeners()
    
    def set_iris_color(self, color: str, side: LcdSide = LcdSide.BOTH) -> None:
        """
        仅设置虹膜颜色/样式 (保持当前主题)
        """
        if color.startswith("color_"): color = color.upper()
        
        with self._render_lock:
            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                self._left_state.iris_style = color
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                self._right_state.iris_style = color

        if not self._animating:
            self._update_display(side)
        self._notify_state_listeners()
    
    def set_lid(self, top_id: Optional[int] = None, bottom_id: Optional[int] = None,
                side: LcdSide = LcdSide.BOTH) -> None:
        """
        设置眼睑
        
        Args:
            top_id: 上眼睑 ID
            bottom_id: 下眼睑 ID
            side: 目标眼睛
        """
        with self._render_lock:
            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                if top_id is not None: self._left_state.top_lid_id = top_id
                if bottom_id is not None: self._left_state.bottom_lid_id = bottom_id
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                if top_id is not None: self._right_state.top_lid_id = top_id
                if bottom_id is not None: self._right_state.bottom_lid_id = bottom_id
        
        if not self._animating:
            self._update_display(side)
        self._notify_state_listeners()
    
    def set_background(self, style_or_name: Optional[str], type_name: Optional[str] = None, 
                       side: LcdSide = LcdSide.BOTH, duration_ms: int = 0) -> None:
        """
        设置背景（支持两种用法）

        1) set_background(style, type_name="COLOR"/"IMAGE")
        2) set_background(name) - 尝试自动识别类型（如果是 COLOR_* 则为 COLOR，否则在配置中搜索）

        Args:
            style_or_name: 背景样式名称或 None（None=黑色）
            type_name: 可选的类型 (COLOR/IMAGE)
            side: 目标眼睛
            duration_ms: 显示持续时间（毫秒），0 表示永久
        """
        with self._render_lock:
            # 处理 None -> 黑色背景
            if style_or_name is None:
                b_type = 'COLOR'
                b_style = 'COLOR_BLACK'
            else:
                b_style = style_or_name
                if type_name:
                    b_type = type_name.upper()
                else:
                    # 自动识别：如果是 COLOR_ 前缀，直接使用 COLOR
                    if b_style.upper().startswith('COLOR_'):
                        b_type = 'COLOR'
                    else:
                        # 在配置中搜索
                        found = False
                        for t in self._renderer._asset_manager.get_available_background_types():
                            if b_style in self._renderer._asset_manager.get_available_background_styles(t):
                                b_type = t
                                found = True
                                break
                        if not found:
                            b_type = 'IMAGE'

            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                self._left_state.background_type = b_type
                self._left_state.background_style = b_style
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                self._right_state.background_type = b_type
                self._right_state.background_style = b_style

        if not self._animating:
            self._update_display(side)
        self._notify_state_listeners()
        
        # 如果指定了 duration_ms 且 > 0，则在超时后恢复到默认背景
        if duration_ms > 0:
            import threading
            logger.info(f"[set_background] 启动超时线程: 将在 {duration_ms}ms 后恢复到默认背景 (type={self._default_bg_type}, style={self._default_bg_style}, side={side})")
            
            def restore_background_after_timeout():
                import time
                time.sleep(duration_ms / 1000.0)
                logger.info(f"[set_background timeout] 超时触发，恢复到默认背景 (type={self._default_bg_type}, style={self._default_bg_style}, side={side})")
                self.set_background(self._default_bg_style, type_name=self._default_bg_type, side=side)
            
            timer_thread = threading.Thread(target=restore_background_after_timeout, daemon=True)
            timer_thread.start()
            logger.info(f"[set_background] 超时线程已启动")
    
    # 旧的 set_iris_theme/set_iris_color 已被移除以避免与新的 iris_style 实现冲突。
    # 请使用 set_iris_theme(theme, style, side) 和 set_iris_color(style, side)（它们操作 iris_style 字段）。
    
    def set_lid(self, side_id: int = 0, top_id: int = 0, bottom_id: int = 0,
                side: LcdSide = LcdSide.BOTH) -> None:
        """
        设置眼睑
        
        Args:
            side_id: 侧眼睑 ID (0=无)
            top_id: 上眼睑 ID
            bottom_id: 下眼睑 ID
            side: 目标眼睛
        """
        with self._render_lock:
            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                self._left_state.lid_side_id = side_id
                self._left_state.lid_top_id = top_id
                self._left_state.lid_bottom_id = bottom_id
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                self._right_state.lid_side_id = side_id
                self._right_state.lid_top_id = top_id
                self._right_state.lid_bottom_id = bottom_id
        
        if not self._animating:
            self._update_display(side)
        self._notify_state_listeners()
    
    def set_brightness(self, level: int, side: LcdSide = LcdSide.BOTH) -> None:
        """
        设置亮度
        """
        level = max(0, min(10, level))
        
        with self._render_lock:
            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                self._left_state.brightness = level
                self._left_driver.set_brightness(level)
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                self._right_state.brightness = level
                self._right_driver.set_brightness(level)
    
    def set_expression(self, name: str) -> None:
        """
        设置表情预设
        
        Args:
            name: 表情名称 (normal, happy, sad, angry, etc.)
        """
        if name not in EXPRESSIONS:
            logger.warning(f"未知表情: {name}")
            return
        
        expr = EXPRESSIONS[name]
        
        with self._render_lock:
            # 合并表情状态到当前状态 (保留颜色设置)
            left_expr = expr["left"]
            right_expr = expr["right"]
            
            self._left_state.iris_x = left_expr.iris_x
            self._left_state.iris_y = left_expr.iris_y
            self._left_state.top_lid_id = left_expr.top_lid_id
            self._left_state.bottom_lid_id = left_expr.bottom_lid_id
            self._left_state.top_lid_y = left_expr.top_lid_y
            self._left_state.bottom_lid_y = left_expr.bottom_lid_y
            
            self._right_state.iris_x = right_expr.iris_x
            self._right_state.iris_y = right_expr.iris_y
            self._right_state.top_lid_id = right_expr.top_lid_id
            self._right_state.bottom_lid_id = right_expr.bottom_lid_id
            self._right_state.top_lid_y = right_expr.top_lid_y
            self._right_state.bottom_lid_y = right_expr.bottom_lid_y
        
        self._update_display(LcdSide.BOTH)
        logger.debug(f"EyeController: 设置表情 {name}")
    
    def blink(self, duration: float = BLINK_DURATION) -> None:
        """
        执行眨眼动作
        
        Args:
            duration: 眨眼持续时间 (秒) - 仅在执行内置简易逻辑时有效
        """
        logger.debug(f"EyeController.blink requested: animating={self._animating}, duration={duration}")
        if self._animating:
            logger.debug("EyeController.blink ignored:另一个动画正在执行")
            return

        # 如果配置了动画列表并且有回调，则优先播放动画
        if self._blink_animations and self._blink_callback:
            try:
                anims = list(self._blink_animations.keys())
                weights = list(self._blink_animations.values())
                chosen = random.choices(anims, weights=weights, k=1)[0]
                logger.info(f"EyeController: 执行眨眼动画: {chosen}")
                self._blink_callback(chosen)
                return
            except Exception as e:
                logger.error(f"EyeController: 播放眨眼动画失败, 回退到内置逻辑: {e}")

        self._animating = True
        # blink 中不应叠加任何 overlay，因此启用抑制标志
        self._suppress_overlays = True
         
        try:
            # 保存当前状态
            saved_left = self._left_state.copy()
            saved_right = self._right_state.copy()
            
            # 眨眼动画：逐渐关闭 -> 逐渐打开
            frame_duration = duration / (BLINK_FRAMES * 2)
            
            # 关闭眼睛
            for lid_id in [4, 6, 7, 8]:
                with self._render_lock:
                    self._left_state.lid_side_id = lid_id
                    self._right_state.lid_side_id = lid_id
                self._update_display(LcdSide.BOTH)
                time.sleep(frame_duration)
            
            # 打开眼睛
            for lid_id in [7, 6, 4, 0]:
                with self._render_lock:
                    self._left_state.lid_side_id = lid_id
                    self._right_state.lid_side_id = lid_id
                self._update_display(LcdSide.BOTH)
                time.sleep(frame_duration)
            
            # 恢复状态
            with self._render_lock:
                self._left_state = saved_left
                self._right_state = saved_right
            self._update_display(LcdSide.BOTH)
            
        finally:
            # 关闭抑制并结束动画
            self._suppress_overlays = False
            self._animating = False
            logger.info("EyeController: 眨眼完成")
    
    def look_at(self, x: float, y: float) -> None:
        """
        让眼睛看向指定方向
        
        Args:
            x: 水平方向 (-1.0=左, 1.0=右)
            y: 垂直方向 (-1.0=上, 1.0=下)
        """
        self.set_iris_position(x, y, LcdSide.BOTH)
    
    def enable_auto_blink(self, interval_range: tuple = None) -> None:
        """
        启用自动眨眼
        
        Args:
            interval_range: (min_seconds, max_seconds) 眨眼间隔范围
        """
        if self._auto_blink_enabled:
            return
        
        interval_range = interval_range or self._config.blink_interval
        self._auto_blink_enabled = True
        self._blink_stop_event.clear()
        
        self._blink_thread = threading.Thread(
            target=self._auto_blink_loop,
            args=(interval_range,),
            daemon=True
        )
        self._blink_thread.start()
        logger.info(f"EyeController: 自动眨眼已启用, 间隔 {interval_range}")
    
    def disable_auto_blink(self) -> None:
        """禁用自动眨眼"""
        if not self._auto_blink_enabled:
            return
        
        self._auto_blink_enabled = False
        self._blink_stop_event.set()
        
        if self._blink_thread and self._blink_thread.is_alive():
            self._blink_thread.join(timeout=1.0)
        
        self._blink_thread = None
        logger.info("EyeController: 自动眨眼已禁用")
    
    def _auto_blink_loop(self, interval_range: tuple) -> None:
        """自动眨眼线程"""
        min_interval, max_interval = interval_range
        
        while self._auto_blink_enabled and not self._blink_stop_event.is_set():
            # 随机等待
            wait_time = random.uniform(min_interval, max_interval)
            
            # 使用可中断的等待
            if self._blink_stop_event.wait(timeout=wait_time):
                break
            
            # 执行眨眼
            if self._auto_blink_enabled and not self._animating:
                self.blink()
    
    def _update_display(self, side: LcdSide = LcdSide.BOTH) -> None:
        """更新屏幕显示"""
        with self._render_lock:
            # 1. 预检查是否有独占模式的 overlay（按左右眼分开判断）
            has_exclusive_left = False
            has_exclusive_right = False
            active_overlays = []
            # 如果处于动画状态并且设置了抑制标志，则跳过所有 overlay
            if self._overlay_provider and not getattr(self, '_suppress_overlays', False):
                try:
                    active_overlays = self._overlay_provider()
                    for info in active_overlays:
                        if not info.get('exclusive'):
                            continue
                        side_info = info.get('side')
                        if side_info == LcdSide.LEFT or side_info == LcdSide.BOTH:
                            has_exclusive_left = True
                        if side_info == LcdSide.RIGHT or side_info == LcdSide.BOTH:
                            has_exclusive_right = True
                except Exception:
                    pass

            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                base_left = None
                if not has_exclusive_left:
                    img = self._renderer.render(self._left_state, LcdSide.LEFT)
                    data = self._renderer.convert_to_rgb888(img)
                    try:
                        import numpy as _np
                        h, w = img.size[1], img.size[0]
                        base_left = _np.frombuffer(data, dtype=_np.uint8).reshape((h, w, 3))
                    except Exception:
                        base_left = None
                else:
                    # 独占模式：起始为黑色背景
                    try:
                        import numpy as _np
                        base_left = _np.zeros((240, 240, 3), dtype=_np.uint8)
                    except Exception:
                        base_left = None

                # composite overlays
                if base_left is not None:
                    try:
                        # apply overlays that target LEFT or BOTH (use per-side latest_frame_left)
                        for info in active_overlays:
                            side_info = info.get('side')
                            if side_info == LcdSide.LEFT or side_info == LcdSide.BOTH:
                                lock = info.get('frame_lock')
                                if lock:
                                    with lock:
                                        of = info.get('latest_frame_left')
                                        if of is not None:
                                            # of expected shape (H,W,4) or (H,W,3)
                                            if of.shape[2] == 4:
                                                alpha = of[..., 3:4].astype('float32') / 255.0
                                                overlay_rgb = of[..., :3].astype('float32')
                                                base_left = (overlay_rgb * alpha + base_left.astype('float32') * (1 - alpha)).astype('uint8')
                                            else:
                                                base_left = of.copy()
                    except Exception:
                        logger.exception("_update_display: 左眼合成 overlay 失败")

                # Ensure overlays are preserved during display updates
                for info in active_overlays:
                    if info.get('side') == side or info.get('side') == LcdSide.BOTH:
                        lock = info.get('frame_lock')
                        if lock:
                            with lock:
                                # Preserve overlay frames
                                if side == LcdSide.LEFT:
                                    self._left_state.overlay_frame = info.get('latest_frame_left')
                                elif side == LcdSide.RIGHT:
                                    self._right_state.overlay_frame = info.get('latest_frame_right')

                # write back final left frame
                try:
                    if base_left is not None:
                        self._left_driver.write(base_left.tobytes())
                except Exception:
                    logger.exception("_update_display: 左眼写入失败")
            
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                base_right = None
                if not has_exclusive_right:
                    img = self._renderer.render(self._right_state, LcdSide.RIGHT)
                    data = self._renderer.convert_to_rgb888(img)
                    try:
                        import numpy as _np
                        h, w = img.size[1], img.size[0]
                        base_right = _np.frombuffer(data, dtype=_np.uint8).reshape((h, w, 3))
                    except Exception:
                        base_right = None
                else:
                    # 独占模式：起始为黑色背景
                    try:
                        import numpy as _np
                        base_right = _np.zeros((240, 240, 3), dtype=_np.uint8)
                    except Exception:
                        base_right = None

                # composite overlays
                if base_right is not None:
                    try:
                        # apply overlays that target RIGHT or BOTH
                        for info in active_overlays:
                            side_info = info.get('side')
                            if side_info == LcdSide.RIGHT or side_info == LcdSide.BOTH:
                                lock = info.get('frame_lock')
                                if lock:
                                    with lock:
                                        of = info.get('latest_frame_right')
                                        if of is not None:
                                            # of expected shape (H,W,4) or (H,W,3)
                                            if of.shape[2] == 4:
                                                alpha = of[..., 3:4].astype('float32') / 255.0
                                                overlay_rgb = of[..., :3].astype('float32')
                                                base_right = (overlay_rgb * alpha + base_right.astype('float32') * (1 - alpha)).astype('uint8')
                                            else:
                                                base_right = of.copy()
                    except Exception:
                        logger.exception("_update_display: 右眼合成 overlay 失败")

                # write back final right frame
                try:
                    if base_right is not None:
                        self._right_driver.write(base_right.tobytes())
                except Exception:
                    logger.exception("_update_display: 右眼写入失败")

    def update(self) -> None:
        """强制刷新显示"""
        if not self._animating:
            self._update_display(LcdSide.BOTH)
        else:
            logger.debug("EyeController.update() 被忽略：当前正在播放动画")
    
    def reset(self) -> None:
        """重置为默认状态"""
        import traceback
        caller = ''.join(traceback.format_stack(limit=6)[:-1])
        logger.info(f"EyeController.reset() called from:\n{caller}")
        self._left_state = EyeState()
        self._right_state = EyeState()
        if not self._animating:
            self._update_display(LcdSide.BOTH)
        else:
            logger.debug("EyeController.reset() 被忽略：当前正在播放动画")
    
    def set_animating(self, animating: bool) -> None:
        """设置动画状态标志（用于抑制自动眨眼和避免 Controller 写屏）"""
        with self._render_lock:
            self._animating = bool(animating)
    
    def set_blink_callback(self, callback: Callable[[str], None]) -> None:
        """设置眨眼动画播放回调"""
        self._blink_callback = callback

    def set_blink_animations(self, animations: dict) -> None:
        """设置眨眼动画列表 {名称: 权重}"""
        self._blink_animations = animations
