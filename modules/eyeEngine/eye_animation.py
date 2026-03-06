"""
眼睛动画播放器

基于 eyeanimations.xml 配置播放表情动画

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from __future__ import annotations

import logging
import threading
import time
from dataclasses import dataclass
from typing import Callable, Optional, TYPE_CHECKING

from .constants import LCD_WIDTH, LCD_HEIGHT, MIN_FPS, MAX_FPS
from .config import EyeState, LcdSide, EngineConfig
from .config_loader import (
    EyeAnimation, EyeAnimationEvent, EyeAnimationSide,
    get_config_loader
)

if TYPE_CHECKING:
    from .renderer import EyeRenderer
    from .drivers.interfaces import ILcdDriver

logger = logging.getLogger(__name__)


@dataclass
class InterpolatedState:
    """插值后的眼睛状态"""
    x: float
    y: float
    scale_x: float
    scale_y: float
    lid_top_y: float
    lid_bot_y: float
    top_lid_index: int
    bot_lid_index: int
    rotate_angle: float
    ellipse_h: int


class EyeAnimationPlayer:
    """
    眼睛动画播放器
    
    负责解析和播放 eyeanimations.xml 中定义的动画。
    支持关键帧插值、循环播放、中断等功能。
    """
    
    # 特殊值 555 表示保持当前值
    KEEP_CURRENT = 555
    
    def __init__(
        self,
        renderer: EyeRenderer,
        left_driver: ILcdDriver,
        right_driver: ILcdDriver,
        fps: Optional[int] = None,
        overlay_provider: Optional[Callable[[], list]] = None
    ):
        """
        初始化播放器
        
        Args:
            renderer: 眼睛渲染器
            left_driver: 左眼 LCD 驱动
            right_driver: 右眼 LCD 驱动
            fps: 渲染帧率 (默认 25 FPS)
            overlay_provider: 可选的 callable，返回当前活动 overlays 列表（用于在动画播放时叠加 overlay）
        """
        self._renderer = renderer
        self._left_driver = left_driver
        self._right_driver = right_driver
        # Optional overlay provider (callable -> list of info dicts)
        self._overlay_provider = overlay_provider

        # 使用配置默认 FPS（如果未指定），并限制在 MIN/MAX 之间
        desired_fps = fps if fps is not None else EngineConfig().default_fps
        self._fps = max(MIN_FPS, min(MAX_FPS, int(desired_fps)))
        self._frame_time = 1.0 / self._fps

        # 用于计算实际播放帧率
        self._frame_counter = 0
        self._fps_last_time = time.perf_counter()
        self._current_fps = 0.0
        self._play_start_time = 0.0
        self._last_average_fps = 0.0
        self._total_frame_count = 0
        
        # 当前眼睛状态 (用于保持)
        self._left_state = EyeState()
        self._right_state = EyeState()
        
        # 播放控制
        self._playing = False
        self._stop_flag = False
        self._play_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        
        # rendering lock to allow external threads to trigger immediate composite writes
        self._render_lock = threading.Lock()

        # 回调
        self._on_complete: Optional[Callable[[], None]] = None
    
    @property
    def is_playing(self) -> bool:
        """是否正在播放"""
        return self._playing
    
    @property
    def current_fps(self) -> float:
        """返回最近计算的播放帧率（FPS）"""
        with self._lock:
            return self._current_fps

    def set_fps(self, fps: int) -> None:
        """设置播放帧率（会被限制在 MIN_FPS..MAX_FPS 范围内）"""
        with self._lock:
            self._fps = max(MIN_FPS, min(MAX_FPS, int(fps)))
            self._frame_time = 1.0 / self._fps

    def get_fps(self) -> int:
        """获取当前播放帧率"""
        with self._lock:
            return self._fps
    
    def play(
        self,
        animation_name: str,
        blocking: bool = False,
        on_complete: Optional[Callable[[], None]] = None
    ) -> bool:
        """
        播放指定名称的动画
        
        Args:
            animation_name: 动画名称
            blocking: 是否阻塞等待完成
            on_complete: 完成回调
            
        Returns:
            是否成功开始播放
        """
        loader = get_config_loader()
        animation = loader.get_animation_by_name(animation_name)
        
        if animation is None:
            logger.error(f"动画不存在: {animation_name}")
            return False
        
        return self.play_animation(animation, blocking, on_complete)
    
    def play_by_id(
        self,
        anim_id: int,
        blocking: bool = False,
        on_complete: Optional[Callable[[], None]] = None
    ) -> bool:
        """
        播放指定 ID 的动画
        
        Args:
            anim_id: 动画 ID
            blocking: 是否阻塞等待完成
            on_complete: 完成回调
            
        Returns:
            是否成功开始播放
        """
        loader = get_config_loader()
        animation = loader.get_animation_by_id(anim_id)
        
        if animation is None:
            logger.error(f"动画 ID 不存在: {anim_id}")
            return False
        
        return self.play_animation(animation, blocking, on_complete)
    
    def play_animation(
        self,
        animation: EyeAnimation,
        blocking: bool = False,
        on_complete: Optional[Callable[[], None]] = None
    ) -> bool:
        """
        播放动画对象
        
        Args:
            animation: 动画对象
            blocking: 是否阻塞等待完成
            on_complete: 完成回调
            
        Returns:
            是否成功开始播放
        """
        # 停止当前播放
        self.stop()
        
        self._on_complete = on_complete
        self._stop_flag = False
        self._playing = True

        # 重置 FPS 统计
        self._frame_counter = 0
        self._fps_last_time = time.perf_counter()
        self._current_fps = 0.0
        self._total_frame_count = 0
        
        if blocking:
            self._play_animation_internal(animation)
            return True
        else:
            self._play_thread = threading.Thread(
                target=self._play_animation_internal,
                args=(animation,),
                daemon=True
            )
            self._play_thread.start()
            return True
    
    def stop(self):
        """停止当前播放"""
        self._stop_flag = True
        if self._play_thread and self._play_thread.is_alive():
            self._play_thread.join(timeout=1.0)
        self._playing = False
    
    def _play_animation_internal(self, animation: EyeAnimation):
        """内部播放逻辑"""
        try:
            logger.info(f"开始播放动画: {animation.name} (ID={animation.anim_id})")
            self._play_start_time = time.perf_counter()
            
            # 获取左右眼动画序列
            left_events = animation.left.events if animation.left else []
            right_events = animation.right.events if animation.right else []
            
            # 并行处理两只眼睛
            left_player = _SideAnimationPlayer(
                left_events, self._left_state, LcdSide.LEFT
            )
            right_player = _SideAnimationPlayer(
                right_events, self._right_state, LcdSide.RIGHT
            )
            
            # 主播放循环
            last_frame_time = time.perf_counter()
            while not self._stop_flag:
                frame_start = time.perf_counter()

                # 时间增量（秒），基于真实时间而非固定帧长，防止动画跳帧或跑飞
                now = frame_start
                dt = now - last_frame_time
                # 限幅，防止长时间阻塞后一次性推进过多
                if dt > 0.5:
                    dt = self._frame_time
                last_frame_time = now

                # 获取当前状态
                left_done = left_player.is_done
                right_done = right_player.is_done

                if left_done and right_done:
                    break

                # 推进动画（传入实际 dt）
                left_state = left_player.advance(dt)
                right_state = right_player.advance(dt)

                # 更新保存的状态
                if left_state:
                    self._update_eye_state(self._left_state, left_state)
                if right_state:
                    self._update_eye_state(self._right_state, right_state)

                # 渲染
                self._render_frame()

                # 维护目标帧率（睡眠到下一帧起点）
                elapsed = time.perf_counter() - frame_start
                to_sleep = max(0.0, self._frame_time - elapsed)
                if to_sleep > 0:
                    time.sleep(to_sleep)

                # 更新 FPS 统计
                self._frame_counter += 1
                self._total_frame_count += 1
                now_fps = time.perf_counter()
                elapsed_fps = now_fps - self._fps_last_time
                if elapsed_fps >= 1.0:
                    with self._lock:
                        self._current_fps = self._frame_counter / elapsed_fps if elapsed_fps > 0 else 0.0
                    self._frame_counter = 0
                    self._fps_last_time = now_fps
            
            logger.info(f"动画播放完成: {animation.name}")
            
        except Exception as e:
            logger.error(f"动画播放错误: {e}")
        finally:
            # 计算平均帧率作为最终统计
            try:
                total_time = time.perf_counter() - self._play_start_time if self._play_start_time > 0 else 0
                if total_time > 0:
                    self._last_average_fps = (self._total_frame_count / total_time) if total_time > 0 else 0.0
                    with self._lock:
                        self._current_fps = self._last_average_fps
            except Exception:
                pass

            self._playing = False
            # 在播放结束时记录最终状态，便于诊断谁在执行复位/恢复默认
            try:
                with self._lock:
                    left = self._left_state
                    right = self._right_state
                logger.info(
                    f"EyeAnimationPlayer finished animation '{animation.name}' final_state: "
                    f"left={{iris_x:{left.iris_x}, iris_y:{left.iris_y}, top_lid_id:{left.top_lid_id}, bottom_lid_id:{left.bottom_lid_id}}} "
                    f"right={{iris_x:{right.iris_x}, iris_y:{right.iris_y}, top_lid_id:{right.top_lid_id}, bottom_lid_id:{right.bottom_lid_id}}}"
                )
            except Exception:
                logger.exception("记录最终状态时出错")

            if self._on_complete:
                try:
                    self._on_complete()
                except Exception as e:
                    logger.error(f"完成回调错误: {e}")

    def _update_eye_state(self, state: EyeState, interp: InterpolatedState):
        """根据插值结果更新眼睛状态"""
        # 只更新非 KEEP_CURRENT 的值
        if interp.x != self.KEEP_CURRENT:
            state.iris_x = int(interp.x)
        if interp.y != self.KEEP_CURRENT:
            state.iris_y = int(interp.y)
        if interp.scale_x != self.KEEP_CURRENT:
            state.scale_x = interp.scale_x
        if interp.scale_y != self.KEEP_CURRENT:
            state.scale_y = interp.scale_y
        if interp.rotate_angle != self.KEEP_CURRENT:
            state.rotation = interp.rotate_angle
        # 眼睑相关
        state.top_lid_y = int(interp.lid_top_y) if interp.lid_top_y != self.KEEP_CURRENT else state.top_lid_y
        state.bottom_lid_y = int(interp.lid_bot_y) if interp.lid_bot_y != self.KEEP_CURRENT else state.bottom_lid_y
        state.top_lid_id = interp.top_lid_index if interp.top_lid_index != self.KEEP_CURRENT else state.top_lid_id
        state.bottom_lid_id = interp.bot_lid_index if interp.bot_lid_index != self.KEEP_CURRENT else state.bottom_lid_id
    
    def _render_frame(self):
        """渲染当前帧到两个 LCD"""
        # 1. 预检查是否有独占模式的 overlay
        has_exclusive_left = False
        has_exclusive_right = False
        active_overlays = []
        if self._overlay_provider:
            try:
                active_overlays = self._overlay_provider()
                for info in active_overlays:
                    if info.get('exclusive'):
                        side_info = info.get('side')
                        if side_info == LcdSide.LEFT or side_info == LcdSide.BOTH:
                            has_exclusive_left = True
                        if side_info == LcdSide.RIGHT or side_info == LcdSide.BOTH:
                            has_exclusive_right = True
            except Exception:
                pass

        # 渲染左眼
        left_img = self._renderer.render(self._left_state, LcdSide.LEFT)
        left_data = self._renderer.convert_to_rgb888(left_img)
        # Composite overlays (if any) on top of rendered left image while animating
        applied_overlays_left = []
        base_left = None
        try:
            import numpy as _np
            h, w = left_img.size[1], left_img.size[0]
            if not has_exclusive_left:
                base_left = _np.frombuffer(left_data, dtype=_np.uint8).reshape((h, w, 3))
            else:
                base_left = _np.zeros((h, w, 3), dtype=_np.uint8)
        except Exception:
            base_left = None

        if active_overlays and base_left is not None:
            try:
                for info in active_overlays:
                    side_info = info.get('side')
                    if side_info == LcdSide.LEFT or side_info == LcdSide.BOTH:
                        lock = info.get('frame_lock')
                        if lock:
                            with lock:
                                of = info.get('latest_frame_left')
                                if of is not None:
                                    applied_overlays_left.append(info.get('overlay_id'))
                                    # of expected shape (H,W,4) or (H,W,3)
                                    if of.shape[2] == 4:
                                        alpha = of[..., 3:4].astype('float32') / 255.0
                                        overlay_rgb = of[..., :3].astype('float32')
                                        base_left = (overlay_rgb * alpha + base_left.astype('float32') * (1 - alpha)).astype('uint8')
                                    else:
                                        base_left = of.copy()
            except Exception:
                logger.exception("_render_frame: left composite overlay failed")

        try:
            if base_left is not None:
                self._left_driver.write(base_left.tobytes())
            else:
                self._left_driver.write(left_data)
        except Exception:
            logger.exception("_render_frame: left write failed")
         
        # 渲染右眼
        right_img = self._renderer.render(self._right_state, LcdSide.RIGHT)
        right_data = self._renderer.convert_to_rgb888(right_img)
        # Composite overlays (if any) on top of rendered right image while animating
        applied_overlays_right = []
        base_right = None
        try:
            import numpy as _np
            h, w = right_img.size[1], right_img.size[0]
            if not has_exclusive_right:
                base_right = _np.frombuffer(right_data, dtype=_np.uint8).reshape((h, w, 3))
            else:
                base_right = _np.zeros((h, w, 3), dtype=_np.uint8)
        except Exception:
            base_right = None

        if active_overlays and base_right is not None:
            try:
                for info in active_overlays:
                    side_info = info.get('side')
                    if side_info == LcdSide.RIGHT or side_info == LcdSide.BOTH:
                        lock = info.get('frame_lock')
                        if lock:
                            with lock:
                                of = info.get('latest_frame_right')
                                if of is not None:
                                    applied_overlays_right.append(info.get('overlay_id'))
                                    if of.shape[2] == 4:
                                        alpha = of[..., 3:4].astype('float32') / 255.0
                                        overlay_rgb = of[..., :3].astype('float32')
                                        base_right = (overlay_rgb * alpha + base_right.astype('float32') * (1 - alpha)).astype('uint8')
                                    else:
                                        base_right = of.copy()
            except Exception:
                logger.exception("_render_frame: right composite overlay failed")

        try:
            if base_right is not None:
                self._right_driver.write(base_right.tobytes())
            else:
                self._right_driver.write(right_data)
        except Exception:
            logger.exception("_render_frame: right write failed")
        except Exception:
            logger.exception("_render_frame: right write failed")
    
    def set_state(self, left_state: EyeState, right_state: EyeState):
        """设置当前眼睛状态 (用于动画保持)"""
        with self._lock:
            self._left_state = left_state.copy()
            self._right_state = right_state.copy()
    
    def get_state(self):
        """获取当前眼睛状态"""
        with self._lock:
            return self._left_state.copy(), self._right_state.copy()
    
    @property
    def last_average_fps(self) -> float:
        """返回最近一次播放的平均帧率"""
        with self._lock:
            return self._last_average_fps


class _SideAnimationPlayer:
    """单边眼睛动画播放器 (内部使用)"""
    
    KEEP_CURRENT = 555
    
    def __init__(self, events: list, initial_state: EyeState, side: LcdSide):
        self._events = events
        self._side = side
        self._event_index = 0
        self._elapsed = 0.0
        self._current_time = 0.0
        self._in_wait = False
        self._wait_remaining = 0.0
        
        # 当前和目标状态
        self._current = self._state_from_eye_state(initial_state)
        self._target: Optional[InterpolatedState] = None
        self._transition_time = 0.0
        self._transition_elapsed = 0.0
        
        # 初始化第一个事件
        if self._events:
            self._setup_next_event()
    
    @property
    def is_done(self) -> bool:
        """是否播放完成"""
        return self._event_index >= len(self._events) and not self._in_wait
    
    def _state_from_eye_state(self, state: EyeState) -> InterpolatedState:
        """从 EyeState 创建插值状态"""
        return InterpolatedState(
            x=state.iris_x,
            y=state.iris_y,
            scale_x=state.scale_x,
            scale_y=state.scale_y,
            lid_top_y=state.top_lid_y,
            lid_bot_y=state.bottom_lid_y,
            top_lid_index=state.top_lid_id,
            bot_lid_index=state.bottom_lid_id,
            rotate_angle=state.rotation,
            ellipse_h=0
        )
    
    def _state_from_event(self, event: EyeAnimationEvent) -> InterpolatedState:
        """从事件创建插值状态"""
        return InterpolatedState(
            x=event.x,
            y=event.y,
            scale_x=event.scale_x,
            scale_y=event.scale_y,
            lid_top_y=event.lid_top_y,
            lid_bot_y=event.lid_bot_y,
            top_lid_index=event.top_lid_index,
            bot_lid_index=event.bot_lid_index,
            rotate_angle=event.rotate_angle,
            ellipse_h=event.ellipse_h
        )
    
    def _setup_next_event(self):
        """设置下一个事件"""
        if self._event_index >= len(self._events):
            return
            
        event = self._events[self._event_index]
        self._target = self._state_from_event(event)
        self._transition_time = event.time_ms / 1000.0
        self._transition_elapsed = 0.0
        self._wait_remaining = event.wait / 1000.0
    
    def advance(self, dt: float) -> Optional[InterpolatedState]:
        """
        推进动画
        
        Args:
            dt: 时间增量 (秒)
            
        Returns:
            当前插值状态，或 None 如果无变化
        """
        if self.is_done:
            return None
        
        # 处理等待阶段
        if self._in_wait:
            self._wait_remaining -= dt
            if self._wait_remaining <= 0:
                self._in_wait = False
                self._event_index += 1
                if self._event_index < len(self._events):
                    self._setup_next_event()
            return self._current
        
        # 处理过渡阶段
        if self._target is None:
            return self._current
        
        self._transition_elapsed += dt
        
        # 计算插值进度
        if self._transition_time <= 0:
            progress = 1.0
        else:
            progress = min(1.0, self._transition_elapsed / self._transition_time)
        
        # 线性插值
        self._current = self._interpolate(self._current, self._target, progress)
        
        # 过渡完成
        if progress >= 1.0:
            self._current = self._target
            
            # 进入等待阶段
            if self._wait_remaining > 0:
                self._in_wait = True
            else:
                self._event_index += 1
                if self._event_index < len(self._events):
                    self._setup_next_event()
        
        return self._current
    
    def _interpolate(
        self,
        start: InterpolatedState,
        end: InterpolatedState,
        t: float
    ) -> InterpolatedState:
        """线性插值两个状态"""
        def lerp(a, b, t):
            if a == self.KEEP_CURRENT or b == self.KEEP_CURRENT:
                return a if a != self.KEEP_CURRENT else b
            return a + (b - a) * t
        
        return InterpolatedState(
            x=lerp(start.x, end.x, t),
            y=lerp(start.y, end.y, t),
            scale_x=lerp(start.scale_x, end.scale_x, t),
            scale_y=lerp(start.scale_y, end.scale_y, t),
            lid_top_y=lerp(start.lid_top_y, end.lid_top_y, t),
            lid_bot_y=lerp(start.lid_bot_y, end.lid_bot_y, t),
            top_lid_index=end.top_lid_index,  # 眼睑 ID 不插值
            bot_lid_index=end.bot_lid_index,
            rotate_angle=lerp(start.rotate_angle, end.rotate_angle, t),
            ellipse_h=int(lerp(start.ellipse_h, end.ellipse_h, t))
        )


# ============================================================================
# 便捷函数
# ============================================================================

def list_animations() -> list:
    """列出所有可用动画名称"""
    loader = get_config_loader()
    return loader.get_animation_names()


def list_categories() -> list:
    """列出所有动画分类"""
    loader = get_config_loader()
    return loader.get_all_categories()


def get_animations_in_category(category: str) -> list:
    """获取指定分类的动画"""
    loader = get_config_loader()
    return [a.name for a in loader.get_animations_by_category(category)]
