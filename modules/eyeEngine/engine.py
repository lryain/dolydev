"""
眼睛动画引擎主类

统一管理所有子模块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
import time
from pathlib import Path
from typing import Optional, Dict, Callable
import threading
import numpy as np

from .config import EyeState, EngineConfig, LcdSide
from .constants import LCD_WIDTH, LCD_HEIGHT, MIN_FPS, MAX_FPS
from .exceptions import NotInitializedError, LcdDriverError
from .drivers import ILcdDriver
from .assets import AssetManager
from .renderer import EyeRenderer
from .controller import EyeController
from .sequence import SeqPlayer
from .behavior import BehaviorManager

logger = logging.getLogger(__name__)


class EyeEngine:

    def play_sprite_animation(self, category: str, animation: str, start: int = 0, loop: bool = False, loop_count: int = None, speed: float = 1.0, clear_time: int = 0, side: str = "BOTH") -> str:
        """
        支持 AnimationSide 节点事件驱动的 SpriteAnimation 动画播放，支持 loop/loop_count/speed/clear_time/side
        """
        self._check_initialized()
        logger.info(f"[ENGINE] play_sprite_animation: category={category}, animation={animation}, start={start}, loop={loop}, loop_count={loop_count}, speed={speed}, clear_time={clear_time}, side={side}")
        import xml.etree.ElementTree as ET
        import threading
        anim_xml = Path(__file__).parent.parent.parent / "assets" / "config" / "eye" / "eyeanimations.xml"
        try:
            tree = ET.parse(str(anim_xml))
            root = tree.getroot()  # <List>
            for anim in root.findall("SpriteAnimation"):
                name = anim.get("Name") or anim.get("name")
                categories = anim.get("Categories") or anim.get("categories") or ""
                cat_list = [c.strip().upper() for c in categories.split(",") if c.strip()]
                if name and name.upper() == animation.upper() and (not category or category.upper() in cat_list):
                    sprite = anim.get("Sprites") or anim.get("sprite")
                    xml_side = anim.get("side", "BOTH")
                    xml_loop = anim.get("loop", "false").lower() == "true"
                    xml_speed = float(anim.get("speed")) if anim.get("speed") else 1.0
                    xml_clear_time = int(anim.get("ClearTime", 0))
                    # 解析 AnimationSide 节点
                    anim_sides = {"LEFT": [], "RIGHT": []}
                    for anim_side in anim.findall("AnimationSide"):
                        side_name = anim_side.get("Side", "BOTH").upper()
                        for event in anim_side.findall("Event"):
                            ev = {
                                "x": int(event.get("X", 0)),
                                "y": int(event.get("Y", 0)),
                                "scalex": float(event.get("ScaleX", 1.0)),
                                "scaley": float(event.get("ScaleY", 1.0)),
                                "rotation": float(event.get("RotateAngle", 0)),
                                "timems": int(event.get("TimeMs", 100)),
                                "wait": int(event.get("Wait", 0)),
                                "easing": event.get("easing", "linear"),
                                "alpha": float(event.get("alpha", 1.0)),
                            }
                            if side_name in ["LEFT", "BOTH"]:
                                anim_sides["LEFT"].append(ev)
                            if side_name in ["RIGHT", "BOTH"]:
                                anim_sides["RIGHT"].append(ev)
                    # 参数优先级：外部传入 > xml 默认
                    use_side = side if side else xml_side
                    use_loop = loop
                    use_speed = speed if speed is not None else xml_speed
                    use_clear_time = clear_time if clear_time is not None else xml_clear_time
                    # loop_count 优先生效
                    use_loop_count = loop_count
                    # 若无 AnimationSide 则兼容原有静态逻辑
                    if not anim_sides["LEFT"] and not anim_sides["RIGHT"]:
                        return self.play_overlay_sprite(sprite=sprite, side=LcdSide[use_side.upper()] if use_side.upper() in ["LEFT", "RIGHT", "BOTH"] else LcdSide.BOTH)
                    # 否则启动动画线程，传递 clear_time、loop、loop_count、speed、side
                    return self._start_sprite_animation_player(sprite, anim_sides, use_loop, use_speed, use_clear_time, use_loop_count, use_side)
            logger.error(f"[ENGINE] play_sprite_animation: 未找到动画: category={category}, animation={animation}")
            return None
        except Exception as e:
            logger.error(f"[ENGINE] play_sprite_animation: 解析 eyeanimations.xml 失败: {e}")
            return None

    def _start_sprite_animation_player(self, sprite, anim_sides, loop, speed, clear_time=0, loop_count=None, side="BOTH"):
        """
        启动 SpriteAnimationPlayer 线程，驱动 overlay 多帧动画
        """
        import threading
        from PIL import Image
        import numpy as np
        # 解析图片
        sprite_path_left = None
        sprite_path_right = None
        try:
            import xml.etree.ElementTree as ET
            sprites_xml = Path(__file__).parent.parent.parent / "assets" / "config" / "eye" / "sprites.xml"
            tree = ET.parse(str(sprites_xml))
            root = tree.getroot()
            for stype in root.findall("Sprite_Type"):
                for s in stype.findall("Sprite"):
                    if s.get("style", "").upper() == sprite.upper():
                        sprite_path_left = s.get("left")
                        sprite_path_right = s.get("right")
                        break
        except Exception as e:
            logger.error(f"[ENGINE] _start_sprite_animation_player: sprites.xml 解析失败: {e}")
        def resolve_path(p):
            if not p:
                return None
            p = p.replace("/.doly/images/", str(Path(self._config.images_path)) + "/")
            path_obj = Path(p)
            if not path_obj.exists():
                logger.error(f"[ENGINE] _start_sprite_animation_player: 图片不存在: {p}")
                return None
            return path_obj
        left_img = resolve_path(sprite_path_left)
        right_img = resolve_path(sprite_path_right) if sprite_path_right else left_img
        if not left_img:
            logger.error(f"[ENGINE] _start_sprite_animation_player: 未找到精灵图片: {sprite}")
            return None
        overlay_id = f"spriteanim_{sprite}_{int(time.time()*1000)}"
        frame_lock = threading.Lock()
        overlay_info = {
            "overlay_id": overlay_id,
            "side": LcdSide.BOTH,
            "sprite": sprite,
            "params": {},
            "frames": {},
            "latest_frame_left": None,
            "latest_frame_right": None,
            "frame_lock": frame_lock,
            "player_thread": None,
            "_stop_flag": False,
        }
        self._overlays[overlay_id] = overlay_info
        # 缓动曲线插值函数
        def lerp(a, b, t):
            return a + (b - a) * t

        def ease_in(t):
            return t * t
        def ease_out(t):
            return 1 - (1 - t) * (1 - t)
        def ease_in_out(t):
            return 0.5 * (1 - cos(pi * t))
        def bounce(t):
            # 简单弹跳曲线
            if t < 0.5:
                return 4 * t * (1 - t)
            else:
                return 1 - pow(-2 * t + 2, 2) / 2
        def elastic(t):
            # 简单弹性曲线
            c4 = (2 * 3.14159265) / 3
            if t == 0 or t == 1:
                return t
            return pow(2, -10 * t) * sin((t * 10 - 0.75) * c4) + 1
        from math import sin, cos, pi, pow
        def get_easing_func(name):
            name = (name or "linear").lower()
            if name == "linear":
                return lambda t: t
            if name == "ease-in":
                return ease_in
            if name == "ease-out":
                return ease_out
            if name == "ease-in-out":
                return ease_in_out
            if name == "bounce":
                return bounce
            if name == "elastic":
                return elastic
            return lambda t: t

        def anim_thread():
            n_left = len(anim_sides["LEFT"])
            n_right = len(anim_sides["RIGHT"])
            def get_init(ev):
                return {
                    "x": ev["x"] if ev else 0,
                    "y": ev["y"] if ev else 0,
                    "scalex": ev["scalex"] if ev else 1.0,
                    "scaley": ev["scaley"] if ev else 1.0,
                    "rotation": ev["rotation"] if ev else 0.0,
                    "alpha": ev["alpha"] if ev and "alpha" in ev else 1.0,
                }
            state = {"LEFT": get_init(anim_sides["LEFT"][0] if n_left else None), "RIGHT": get_init(anim_sides["RIGHT"][0] if n_right else None)}
            played_count = 0
            while not overlay_info["_stop_flag"]:
                max_steps = max(n_left, n_right)
                for step in range(max_steps):
                    # 同步步进，左右眼参数分别插值
                    prev_left = state["LEFT"]
                    prev_right = state["RIGHT"]
                    ev_left = anim_sides["LEFT"][step % n_left] if n_left else None
                    ev_right = anim_sides["RIGHT"][step % n_right] if n_right else None
                    # 取最大帧数，保证左右同步
                    duration_left = max(1, int(ev_left["timems"])) if ev_left else 1
                    duration_right = max(1, int(ev_right["timems"])) if ev_right else 1
                    frames_left = max(1, int(duration_left / 20))
                    frames_right = max(1, int(duration_right / 20))
                    frames = max(frames_left, frames_right)
                    # 获取缓动函数
                    easing_left = get_easing_func(ev_left["easing"]) if ev_left and "easing" in ev_left else (lambda t: t)
                    easing_right = get_easing_func(ev_right["easing"]) if ev_right and "easing" in ev_right else (lambda t: t)
                    alpha_left = float(ev_left["alpha"]) if ev_left and "alpha" in ev_left else 1.0
                    alpha_right = float(ev_right["alpha"]) if ev_right and "alpha" in ev_right else 1.0
                    for f in range(frames):
                        t_left = min(1.0, (f + 1) / frames_left) if ev_left else 1.0
                        t_right = min(1.0, (f + 1) / frames_right) if ev_right else 1.0
                        t_left_eased = easing_left(t_left)
                        t_right_eased = easing_right(t_right)
                        # 左右参数插值
                        if ev_left:
                            _x_l = int(lerp(prev_left["x"], ev_left["x"], t_left_eased))
                            _y_l = int(lerp(prev_left["y"], ev_left["y"], t_left_eased))
                            _scalex_l = lerp(prev_left["scalex"], ev_left["scalex"], t_left_eased)
                            _scaley_l = lerp(prev_left["scaley"], ev_left["scaley"], t_left_eased)
                            _rotation_l = lerp(prev_left["rotation"], ev_left["rotation"], t_left_eased)
                            _alpha_l = lerp(prev_left["alpha"], alpha_left, t_left_eased)
                        else:
                            _x_l, _y_l, _scalex_l, _scaley_l, _rotation_l, _alpha_l = prev_left["x"], prev_left["y"], prev_left["scalex"], prev_left["scaley"], prev_left["rotation"], prev_left.get("alpha", 1.0)
                        if ev_right:
                            _x_r = int(lerp(prev_right["x"], ev_right["x"], t_right_eased))
                            _y_r = int(lerp(prev_right["y"], ev_right["y"], t_right_eased))
                            _scalex_r = lerp(prev_right["scalex"], ev_right["scalex"], t_right_eased)
                            _scaley_r = lerp(prev_right["scaley"], ev_right["scaley"], t_right_eased)
                            _rotation_r = lerp(prev_right["rotation"], ev_right["rotation"], t_right_eased)
                            _alpha_r = lerp(prev_right["alpha"], alpha_right, t_right_eased)
                        else:
                            _x_r, _y_r, _scalex_r, _scaley_r, _rotation_r, _alpha_r = prev_right["x"], prev_right["y"], prev_right["scalex"], prev_right["scaley"], prev_right["rotation"], prev_right.get("alpha", 1.0)
                        # 渲染左右帧，应用透明度
                        try:
                            img_l = Image.open(str(left_img)).convert('RGBA')
                            new_wl = int(img_l.width * _scalex_l)
                            new_hl = int(img_l.height * _scaley_l)
                            img_l = img_l.resize((new_wl, new_hl), Image.Resampling.LANCZOS)
                            if _rotation_l != 0.0:
                                img_l = img_l.rotate(_rotation_l, expand=True, resample=Image.Resampling.BICUBIC)
                            # 应用透明度
                            if _alpha_l < 1.0:
                                alpha_mask = img_l.split()[3].point(lambda p: int(p * _alpha_l))
                                img_l.putalpha(alpha_mask)
                            canvas_l = Image.new('RGBA', (self._renderer._width, self._renderer._height), (0,0,0,0))
                            canvas_l.paste(img_l, (_x_l, _y_l), img_l)
                            img_r = Image.open(str(right_img)).convert('RGBA')
                            new_wr = int(img_r.width * _scalex_r)
                            new_hr = int(img_r.height * _scaley_r)
                            img_r = img_r.resize((new_wr, new_hr), Image.Resampling.LANCZOS)
                            if _rotation_r != 0.0:
                                img_r = img_r.rotate(_rotation_r, expand=True, resample=Image.Resampling.BICUBIC)
                            if _alpha_r < 1.0:
                                alpha_mask = img_r.split()[3].point(lambda p: int(p * _alpha_r))
                                img_r.putalpha(alpha_mask)
                            canvas_r = Image.new('RGBA', (self._renderer._width, self._renderer._height), (0,0,0,0))
                            canvas_r.paste(img_r, (_x_r, _y_r), img_r)
                            with frame_lock:
                                overlay_info["latest_frame_left"] = np.array(canvas_l)
                                overlay_info["latest_frame_right"] = np.array(canvas_r)
                            logger.debug(f"[ENGINE] SpriteAnimationPlayer: step={step}, frame={f}, L: x={_x_l}, y={_y_l}, scalex={_scalex_l}, scaley={_scaley_l}, rotation={_rotation_l}, alpha={_alpha_l}, easing={ev_left['easing'] if ev_left else 'linear'}; R: x={_x_r}, y={_y_r}, scalex={_scalex_r}, scaley={_scaley_r}, rotation={_rotation_r}, alpha={_alpha_r}, easing={ev_right['easing'] if ev_right else 'linear'}")
                        except Exception as e:
                            logger.error(f"[ENGINE] SpriteAnimationPlayer: 图片处理失败: {left_img}/{right_img}, {e}")
                        # 刷新
                        try:
                            if self._controller:
                                self._controller.update()
                        except Exception as e:
                            logger.debug(f"[ENGINE] SpriteAnimationPlayer: controller.update() 异常: {e}")
                        time.sleep(max(1, int(max(duration_left, duration_right) / frames)) / 1000.0 / speed)
                    # 过渡完成后，保持目标参数 wait ms
                    if ev_left:
                        state["LEFT"] = {
                            "x": ev_left["x"],
                            "y": ev_left["y"],
                            "scalex": ev_left["scalex"],
                            "scaley": ev_left["scaley"],
                            "rotation": ev_left["rotation"],
                            "alpha": ev_left["alpha"] if "alpha" in ev_left else 1.0,
                        }
                    if ev_right:
                        state["RIGHT"] = {
                            "x": ev_right["x"],
                            "y": ev_right["y"],
                            "scalex": ev_right["scalex"],
                            "scaley": ev_right["scaley"],
                            "rotation": ev_right["rotation"],
                            "alpha": ev_right["alpha"] if "alpha" in ev_right else 1.0,
                        }
                    wait_l = int(ev_left["wait"]) if ev_left else 0
                    wait_r = int(ev_right["wait"]) if ev_right else 0
                    wait_ms = max(wait_l, wait_r)
                    if wait_ms > 0:
                        try:
                            with frame_lock:
                                overlay_info["latest_frame_left"] = np.array(canvas_l)
                                overlay_info["latest_frame_right"] = np.array(canvas_r)
                            if self._controller:
                                self._controller.update()
                        except Exception as e:
                            logger.error(f"[ENGINE] SpriteAnimationPlayer: 等待阶段刷新失败: {e}")
                        time.sleep(wait_ms / 1000.0 / speed)
                # 循环控制：loop_count > 0 时优先生效
                if loop_count is not None and loop_count > 0:
                    played_count += 1
                    if played_count >= loop_count:
                        break
                elif not loop:
                    break
            # 动画结束后自动清除 overlay
            # 优先用 clear_time，否则用最后一帧wait
            total_clear = clear_time if clear_time > 0 else 0
            if total_clear == 0:
                # 取最后一帧wait
                last_left = anim_sides["LEFT"][-1]["wait"] if n_left else 0
                last_right = anim_sides["RIGHT"][-1]["wait"] if n_right else 0
                total_clear = max(last_left, last_right)
            if total_clear > 0:
                time.sleep(total_clear / 1000.0)
            # 清除 overlay 并强制刷新两次，彻底消除残影
            try:
                with frame_lock:
                    if overlay_id in self._overlays:
                        del self._overlays[overlay_id]
                logger.info(f"[ENGINE] SpriteAnimationPlayer: overlay_id={overlay_id} 已自动清除，准备强制刷新 controller")
                if self._controller:
                    self._controller.update()
                    time.sleep(0.02)  # 20ms 间隔，确保 LCD 刷新
                    self._controller.update()
                logger.info(f"[ENGINE] SpriteAnimationPlayer: overlay_id={overlay_id} 清理后已强制刷新 controller 两次")
            except Exception as e:
                logger.error(f"[ENGINE] SpriteAnimationPlayer: 自动清除 overlay 失败: {e}")
            logger.info(f"[ENGINE] SpriteAnimationPlayer: 动画线程退出 overlay_id={overlay_id}")
        t = threading.Thread(target=anim_thread, daemon=True)
        overlay_info["player_thread"] = t
        t.start()
        logger.info(f"[ENGINE] SpriteAnimationPlayer: 动画线程已启动 overlay_id={overlay_id}")
        return overlay_id

    def play_overlay_sprite(self, sprite: str, side: LcdSide = LcdSide.BOTH, loop: bool = False, fps: int = None, speed: float = 1.0,
                           x=None, y=None, scale=None, rotation=None, duration_ms=None) -> str:
        """
        叠加播放 PNG 精灵动画，支持动画参数（x, y, scale, rotation, duration_ms），后续可扩展为多帧动画。
        """
        self._check_initialized()
        logger.info(f"[ENGINE] play_overlay_sprite: sprite={sprite}, side={side}, loop={loop}, fps={fps}, speed={speed}, x={x}, y={y}, scale={scale}, rotation={rotation}, duration_ms={duration_ms}")

        # 1. 解析 sprites.xml，找到 sprite 对应的图片路径（支持左右眼）
        sprite_path_left = None
        sprite_path_right = None
        try:
            import xml.etree.ElementTree as ET
            # 修正为 config/eye/sprites.xml
            sprites_xml = Path(__file__).parent.parent.parent / "assets" / "config" / "eye" / "sprites.xml"
            tree = ET.parse(str(sprites_xml))
            root = tree.getroot()
            for stype in root.findall("Sprite_Type"):
                for s in stype.findall("Sprite"):
                    if s.get("style", "").upper() == sprite.upper():
                        sprite_path_left = s.get("left")
                        sprite_path_right = s.get("right")
                        break
        except Exception as e:
            logger.error(f"[ENGINE] play_overlay_sprite: sprites.xml 解析失败: {e}")

        # 2. 选取图片路径（支持左右眼）
        def resolve_path(p):
            if not p:
                return None
            # 支持绝对路径和相对路径
            p = p.replace("/.doly/images/", str(Path(self._config.images_path)) + "/")
            path_obj = Path(p)
            if not path_obj.exists():
                logger.error(f"[ENGINE] play_overlay_sprite: 图片不存在: {p}")
                return None
            return path_obj

        left_img = resolve_path(sprite_path_left)
        right_img = resolve_path(sprite_path_right) if sprite_path_right else left_img
        if not left_img:
            logger.error(f"[ENGINE] play_overlay_sprite: 未找到精灵图片: {sprite}")
            return None

        # 3. 生成动画帧，按参数实时渲染，合成到 overlay
        # 这里只实现单帧静态精灵，动画参数可扩展
        from PIL import Image
        overlay_frames = {}
        overlay_np = {}
        import numpy as np
        for s in [LcdSide.LEFT, LcdSide.RIGHT]:
            if side in [LcdSide.BOTH, s]:
                img_path = left_img if s == LcdSide.LEFT else right_img
                try:
                    img = Image.open(str(img_path)).convert('RGBA')
                    _x = int(x) if x is not None else (self._renderer._width // 2 - img.width // 2)
                    _y = int(y) if y is not None else (self._renderer._height // 2 - img.height // 2)
                    _scale = float(scale) if scale is not None else 1.0
                    _rotation = float(rotation) if rotation is not None else 0.0
                    if _scale != 1.0:
                        new_w = int(img.width * _scale)
                        new_h = int(img.height * _scale)
                        img = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
                    if _rotation != 0.0:
                        img = img.rotate(_rotation, expand=True, resample=Image.Resampling.BICUBIC)
                    canvas = Image.new('RGBA', (self._renderer._width, self._renderer._height), (0,0,0,0))
                    canvas.paste(img, (_x, _y), img)
                    overlay_frames[s] = canvas
                    overlay_np[s] = np.array(canvas)
                    logger.info(f"[ENGINE] play_overlay_sprite: side={s.name}, x={_x}, y={_y}, scale={_scale}, rotation={_rotation}")
                except Exception as e:
                    logger.error(f"[ENGINE] play_overlay_sprite: 图片处理失败: {img_path}, {e}")

        # 4. 支持 loop、duration_ms 控制（这里只做静态帧，动画/多帧后续扩展）
        # TODO: 多帧动画、参数插值、定时器/线程驱动

        # 5. 记录 overlay_id，便于后续停止/管理
        overlay_id = f"sprite_{sprite}_{int(time.time()*1000)}"
        # 结构与 play_sequence_animations 保持一致，便于 controller 合成
        overlay_info = {
            "overlay_id": overlay_id,
            "side": side,
            "sprite": sprite,
            "params": dict(x=x, y=y, scale=scale, rotation=rotation, duration_ms=duration_ms, loop=loop, fps=fps, speed=speed),
            "frames": overlay_frames,
            "latest_frame_left": overlay_np.get(LcdSide.LEFT),
            "latest_frame_right": overlay_np.get(LcdSide.RIGHT),
            "frame_lock": threading.Lock(),
        }
        self._overlays[overlay_id] = overlay_info
        logger.info(f"[ENGINE] play_overlay_sprite: overlay_id={overlay_id} 已注册，帧数={len(overlay_frames)}")
        logger.debug(f"[ENGINE] play_overlay_sprite: overlays dict: {self._overlays[overlay_id]}")
        logger.debug(f"[ENGINE] play_overlay_sprite: overlays keys: {list(self._overlays.keys())}")
        # 主动触发 controller.update()，并详细日志
        try:
            if self._controller:
                logger.debug(f"[ENGINE] play_overlay_sprite: 调用 controller.update() 刷新 LCD，overlay_id={overlay_id}")
                before_state = None
                try:
                    before_state = self._controller.get_state(side if side in [LcdSide.LEFT, LcdSide.RIGHT] else LcdSide.LEFT)
                    logger.debug(f"[ENGINE] play_overlay_sprite: controller state before update: {before_state}")
                except Exception as e:
                    logger.debug(f"[ENGINE] play_overlay_sprite: 获取 controller state 前异常: {e}")
                self._controller.update()
                after_state = None
                try:
                    after_state = self._controller.get_state(side if side in [LcdSide.LEFT, LcdSide.RIGHT] else LcdSide.LEFT)
                    logger.debug(f"[ENGINE] play_overlay_sprite: controller state after update: {after_state}")
                except Exception as e:
                    logger.debug(f"[ENGINE] play_overlay_sprite: 获取 controller state 后异常: {e}")
                logger.debug(f"[ENGINE] play_overlay_sprite: controller.update() 调用完成，overlay_id={overlay_id}")
        except Exception as e:
            logger.error(f"[ENGINE] play_overlay_sprite: controller.update() 异常: {e}")
        # 打印所有 overlays 当前内容
        logger.debug(f"[ENGINE] play_overlay_sprite: overlays 全部: {self._overlays}")
        return overlay_id
    def play_overlay_image(self, image: str, side: LcdSide = LcdSide.BOTH, loop: bool = False, fps: Optional[int] = None, speed: float = 1.0) -> Optional[str]:
        """
        以 overlay 方式叠加一张图片（PNG/JPG），可选循环。实现方式：将图片转为单帧序列，复用 play_sequence_animations 机制。
        """
        self._check_initialized()
        if not image:
            logger.error("play_overlay_image: missing image")
            return None

        # 支持直接传入图片路径或图片名（自动补全路径）
        candidate = Path(image)
        if candidate.is_file():
            # 生成临时 .seq 文件（单帧）
            from PIL import Image
            import numpy as np
            img = Image.open(str(candidate)).convert('RGBA')
            arr = np.array(img)
            # 保存为临时 .seq 文件
            import tempfile
            tmp = tempfile.NamedTemporaryFile(delete=False, suffix='.seq')
            seq_path = Path(tmp.name)
            # 这里假设有 save_seq 单帧保存工具
            try:
                from .sequence.save_seq_util import save_seq
                save_seq(arr, str(seq_path))
            except Exception as e:
                logger.error(f"play_overlay_image: save_seq 失败: {e}")
                return None
            seq_name = str(seq_path)
        else:
            # 尝试 assets/images/overlay/ 目录
            img_path = Path(self._config.images_path) / "overlay" / image
            if img_path.exists():
                # 生成临时 .seq 文件
                from PIL import Image
                import numpy as np
                img = Image.open(str(img_path)).convert('RGBA')
                arr = np.array(img)
                import tempfile
                tmp = tempfile.NamedTemporaryFile(delete=False, suffix='.seq')
                seq_path = Path(tmp.name)
                try:
                    from .sequence.save_seq_util import save_seq
                    save_seq(arr, str(seq_path))
                except Exception as e:
                    logger.error(f"play_overlay_image: save_seq 失败: {e}")
                    return None
                seq_name = str(seq_path)
            else:
                logger.error(f"play_overlay_image: 找不到图片: {image}")
                return None

        # 复用 play_sequence_animations 播放单帧序列
        return self.play_sequence_animations(seq_name, side=side, loop=loop, fps=fps, speed=speed)
    """
    Doly 眼睛动画引擎
    
    主入口类，统一管理所有子模块：
    - LCD 驱动
    - 资源管理
    - 眼睛渲染
    - 眼睛控制
    - 序列播放
    
    使用示例:
        ```python
        engine = EyeEngine()
        engine.init()
        
        # 设置眼睛
        engine.controller.set_iris_color("blue")
        engine.controller.set_expression("happy")
        engine.controller.blink()
        
        # 播放动画
        player = engine.play_sequence("hearts.seq")
        
        engine.release()
        ```
    
    上下文管理器使用:
        ```python
        with EyeEngine() as engine:
            engine.controller.set_expression("happy")
        ```
    """
    
    def __init__(self, config: Optional[EngineConfig] = None):
        """
        初始化引擎
        
        Args:
            config: 引擎配置，使用默认配置如果未指定
        """
        self._config = config or EngineConfig()
        
        # 子模块 (延迟初始化)
        self._left_driver: Optional[ILcdDriver] = None
        self._right_driver: Optional[ILcdDriver] = None
        self._asset_manager: Optional[AssetManager] = None
        self._renderer: Optional[EyeRenderer] = None
        self._controller: Optional[EyeController] = None
        self._seq_players: Dict[LcdSide, SeqPlayer] = {}
        self._eye_anim_player = None  # EyeAnimationPlayer
        self._behavior_manager: Optional[BehaviorManager] = None
        
        # 当前状态
        self._left_state = EyeState()
        self._right_state = EyeState()

        # 复位基线与计时器
        self._baseline_left: Optional[EyeState] = None
        self._baseline_right: Optional[EyeState] = None
        self._auto_reset_timer: Optional[threading.Timer] = None
        
        # ★ 新增：overlay 事件发布回调（由 zmq_service 注入）
        self._on_overlay_event: Optional[Callable[[str, Dict], None]] = None
        
        # 状态
        self._initialized = False
        # LCD 暂停状态（用于 widget 显示时暂停 eyeEngine 渲染）
        self._lcd_paused = False
        self._lcd_pause_lock = threading.Lock()
        # overlay 管理: overlay_id -> {players: [SeqPlayer], side: LcdSide, sequence: str, loop: bool,
        #                                 latest_frame: Optional[numpy.ndarray], frame_lock: threading.Lock}
        self._overlays = {}
        self._next_overlay_id = 1

        # 视频流（FaceReco 推流）
        self._video_stream_consumer = None
        self._video_stream_thread = None
        self._video_stream_stop_event = threading.Event()
        self._video_stream_active = False # 控制是否消费视频流
        self._video_stream_overlay_id = None
        self._video_stream_last_frame_ts = 0.0
        self._video_stream_stats = {
            "frames": 0,
            "errors": 0,
            "last_fps": 0.0,
            "last_log_ts": 0.0,
        }
        self._video_stream_pupil_mask = None
        
    def init(self, use_mock: bool = None) -> bool:
        """
        初始化引擎
        
        Args:
            use_mock: 是否使用模拟驱动，None 表示自动检测
            
        Returns:
            成功返回 True
        """
        if self._initialized:
            logger.warning("EyeEngine: 已经初始化")
            return True
        
        try:
            logger.info("EyeEngine: 开始初始化")
            
            # 1. 初始化 LCD 驱动 (左右各一个)
            if use_mock is None:
                use_mock = self._config.use_mock
            
            self._left_driver = self._create_lcd_driver(use_mock, LcdSide.LEFT)
            self._right_driver = self._create_lcd_driver(use_mock, LcdSide.RIGHT)
            
            if not self._left_driver.init():
                raise LcdDriverError("左眼 LCD 驱动初始化失败")
            if not self._right_driver.init():
                raise LcdDriverError("右眼 LCD 驱动初始化失败")
            
            # 2. 初始化资源管理器
            self._asset_manager = AssetManager(self._config)
            
            # 3. 初始化渲染器
            self._renderer = EyeRenderer(self._asset_manager, self._config)
            
            # 4. 初始化控制器 (使用左眼驱动作为主驱动，保持兼容)
            self._controller = EyeController(
                self._left_driver, 
                self._right_driver,
                self._renderer,
                self._config,
                overlay_provider=self._get_active_overlays
            )

            # 注册控制器状态监听器，以便在状态变化时同步动画播放器
            self._controller.add_state_listener(self._sync_player_state)
            
            # 5. 初始化序列播放器
            self._seq_players[LcdSide.LEFT] = SeqPlayer(self._left_driver)
            self._seq_players[LcdSide.RIGHT] = SeqPlayer(self._right_driver)
            
            # 6. 初始化眼睛动画播放器
            from .eye_animation import EyeAnimationPlayer
            self._eye_anim_player = EyeAnimationPlayer(
                self._renderer,
                self._left_driver,
                self._right_driver,
                fps=self._config.default_fps,
                overlay_provider=self._get_active_overlays
            )
            
            # 7. 初始化行为管理器 (animationlist.xml)
            # 兼容 config/animations 和 assets/config/animations
            config_dir = self._config.config_dir
            config_dir_str = str(config_dir)
            if config_dir_str.endswith("config") and not Path(config_dir_str).exists():
                config_dir_str = config_dir_str.replace("config", "assets/config")
            elif "/config/" in config_dir_str and not Path(config_dir_str).exists():
                config_dir_str = config_dir_str.replace("/config/", "/assets/config/")
            animations_dir = Path(config_dir_str) / "animations"
            # animations_dir = Path(self._config.config_dir.replace("config", "assets/config")) / "animations"

            self._behavior_manager = BehaviorManager(str(animations_dir))

            # 8. 启用自动眨眼 (如果配置且未启用被动模式)
            if not self._config.passive_mode and self._config.auto_blink:
                if self._controller:
                    self._controller.set_blink_callback(self._play_blink_animation)
                    if self._config.blink_animations:
                        self._controller.set_blink_animations(self._config.blink_animations)
                
                self._controller.enable_auto_blink(self._config.blink_interval)
            
            self._initialized = True
            logger.info("EyeEngine: 初始化完成")
            
            # 初始显示
            self.render()
            # 捕获一次基线，防止上层未调用 _apply_default_config
            try:
                self.capture_baseline_state()
            except Exception:
                logger.debug("capture_baseline_state at init failed")

            # 9. 启动视频流消费者（可选）
            if getattr(self._config, "video_stream_enabled", False):
                try:
                    self.start_video_stream()
                except Exception:
                    logger.exception("EyeEngine: 视频流启动失败")
            
            return True
            
        except Exception as e:
            logger.error(f"EyeEngine: 初始化失败: {e}")
            self._cleanup()
            raise
    
    def set_video_stream_active(self, active: bool) -> None:
        self._video_stream_active = active
        logger.info(f"EyeEngine: 视频流 active 状态设置为 {active}")

    def _create_lcd_driver(self, use_mock: bool, side: LcdSide) -> ILcdDriver:
        """创建 LCD 驱动"""
        if use_mock:
            # 现在强制要求硬件，如果尝试使用 mock 则警告并尝试转硬件
            logger.warning(f"EyeEngine: 强制使用真实硬件，忽略用 mock 请求 (side={side.name})")
        
        # 尝试加载真实驱动
        try:
            from .drivers.lcd_driver import LcdDriver
            lib_path = self._config.lcd_lib_path
            
            # 检查库文件是否存在
            if not Path(lib_path).exists():
                # 尝试查找 .so 文件
                so_path = lib_path.replace('.a', '.so')
                if Path(so_path).exists():
                    lib_path = so_path
                else:
                    raise LcdDriverError(f"LCD 库不存在: {lib_path}")
            
            logger.info(f"EyeEngine: 使用真实 LCD 驱动: {lib_path} (side={side.name})")
            return LcdDriver(lib_path, side)
            
        except ImportError as e:
            logger.error(f"EyeEngine: 无法加载 LCD 驱动: {e}")
            raise LcdDriverError(f"无法加载 LCD 驱动: {e}")
        except Exception as e:
            logger.error(f"EyeEngine: LCD 驱动初始化失败: {e}")
            raise LcdDriverError(f"LCD 驱动初始化失败: {e}")
    
    def release(self) -> None:
        """释放资源"""
        logger.info("EyeEngine: 释放资源")
        self._cleanup()
        self._initialized = False
    
    def _cleanup(self) -> None:
        """清理资源"""
        # 停止视频流
        try:
            self.stop_video_stream()
        except Exception:
            pass
        # 停止自动眨眼
        if self._controller:
            self._controller.disable_auto_blink()
        
        # 停止眼睛动画
        if self._eye_anim_player:
            self._eye_anim_player.stop()
            # 清理 animating 标志
            if self._controller:
                try:
                    self._controller.set_animating(False)
                    self._controller.update()
                except Exception:
                    pass
        
        # 停止所有播放
        for player in self._seq_players.values():
            if player:
                player.stop()
        
        # 释放 LCD
        if self._left_driver:
            try:
                self._left_driver.release()
            except Exception as e:
                logger.error(f"EyeEngine: 释放左眼 LCD 失败: {e}")
        
        if self._right_driver:
            try:
                self._right_driver.release()
            except Exception as e:
                logger.error(f"EyeEngine: 释放右眼 LCD 失败: {e}")
        
        # 清除引用
        self._left_driver = None
        self._right_driver = None
        self._asset_manager = None
        self._renderer = None
        self._controller = None
        self._seq_players.clear()
        self._eye_anim_player = None
        
        # 停止所有 overlay
        for overlay_id, info in list(self._overlays.items()):
            self.stop_overlay_sequence(overlay_id)

    # ==================== Video Stream ====================
    def start_video_stream(self) -> bool:
        """启动 FaceReco 视频流显示（共享内存消费）。"""
        self._check_initialized()
        if self._video_stream_thread and self._video_stream_thread.is_alive():
            return True

        from .video_stream_consumer import VideoStreamConsumer, resize_frame, target_side_from_config

        resource_id = getattr(self._config, "video_stream_resource_id", "facereco_video")
        instance_id = getattr(self._config, "video_stream_instance_id", 0)
        target_side = target_side_from_config(getattr(self._config, "video_stream_target_lcd", "RIGHT"))
        fps = int(getattr(self._config, "video_stream_fps", 15))
        timeout_ms = int(getattr(self._config, "video_stream_timeout_ms", 100))
        display_mode = str(getattr(self._config, "video_stream_display_mode", "overlay")).lower()
        overlay_style = str(getattr(self._config, "video_stream_overlay_style", "full")).lower()

        consumer = VideoStreamConsumer(resource_id=resource_id, instance_id=instance_id)
        if not consumer.initialize():
            logger.error("EyeEngine: 视频流消费者初始化失败")
            return False

        self._video_stream_consumer = consumer
        self._video_stream_stop_event.clear()

        # 创建 overlay
        overlay_id = "video_stream"
        if overlay_id not in self._overlays:
            self._overlays[overlay_id] = {
                "overlay_id": overlay_id,
                "side": target_side,
                "exclusive": display_mode == "exclusive",
                "latest_frame_left": None,
                "latest_frame_right": None,
                "frame_lock": threading.Lock(),
            }
        self._video_stream_overlay_id = overlay_id

        def _loop():
            last_log = time.time()
            last_count = 0
            interval = 1.0 / max(1, fps)
            get_frame_fail_count = 0

            while not self._video_stream_stop_event.is_set():
                # 如果未激活，则不消费，休眠 100ms
                if not self._video_stream_active:
                    time.sleep(0.1)
                    continue

                start = time.time()
                frame, meta = consumer.get_latest_frame(timeout_ms=timeout_ms)
                if frame is not None:
                    frame = resize_frame(frame, 240)

                    if overlay_style == "pupil":
                        import numpy as _np
                        h, w = frame.shape[:2]
                        if (self._video_stream_pupil_mask is None or
                                self._video_stream_pupil_mask.shape[:2] != (h, w)):
                            yy, xx = _np.ogrid[:h, :w]
                            cx, cy = w // 2, h // 2
                            # 🆕 使用可配置的瞳孔半径比例
                            pupil_ratio = float(getattr(self._config, "video_stream_pupil_radius_ratio", 0.35))
                            radius = int(min(h, w) * pupil_ratio)
                            mask = ((xx - cx) ** 2 + (yy - cy) ** 2) <= radius ** 2
                            alpha = _np.zeros((h, w), dtype=_np.uint8)
                            alpha[mask] = 255
                            self._video_stream_pupil_mask = alpha
                        alpha = self._video_stream_pupil_mask
                        frame = _np.dstack((frame, alpha))
                    info = self._overlays.get(overlay_id)
                    if info:
                        lock = info.get("frame_lock")
                        if lock:
                            with lock:
                                if target_side == LcdSide.LEFT:
                                    info["latest_frame_left"] = frame
                                elif target_side == LcdSide.RIGHT:
                                    info["latest_frame_right"] = frame
                                else:
                                    info["latest_frame_left"] = frame
                                    info["latest_frame_right"] = frame
                    try:
                        if self._controller:
                            self._controller.update()
                    except Exception:
                        pass

                    self._video_stream_stats["frames"] += 1
                else:
                    self._video_stream_stats["errors"] += 1
                    get_frame_fail_count += 1
                    # if get_frame_fail_count % 10 == 0:
                    #     logger.warning("[EyeEngine] 视频帧获取失败 #%d (FaceReco 可能未运行或超时 timeout_ms=%d)", 
                    #                   get_frame_fail_count, timeout_ms)
                    time.sleep(0.005)
                    continue

                now = time.time()
                if now - last_log >= 2.0:
                    count = self._video_stream_stats["frames"]
                    fps_calc = (count - last_count) / (now - last_log)
                    self._video_stream_stats["last_fps"] = fps_calc
                    self._video_stream_stats["last_log_ts"] = now
                    last_log = now
                    last_count = count
                    logger.info("[EyeEngine] video_stream fps=%.1f frames=%s errors=%s get_frame_fail=%s", 
                               fps_calc, count, self._video_stream_stats["errors"], get_frame_fail_count)

                elapsed = time.time() - start
                if elapsed < interval:
                    time.sleep(interval - elapsed)

        self._video_stream_thread = threading.Thread(target=_loop, daemon=True, name="eyeengine_video_stream")
        self._video_stream_thread.start()
        logger.info("EyeEngine: 视频流线程已启动 (resource_id=%s, target=%s, mode=%s, style=%s)", resource_id, target_side, display_mode, overlay_style)
        return True

    def stop_video_stream(self) -> None:
        if self._video_stream_thread and self._video_stream_thread.is_alive():
            self._video_stream_stop_event.set()
            self._video_stream_thread.join(timeout=1.0)
        self._video_stream_thread = None
        self._video_stream_stop_event.clear()
        if self._video_stream_consumer:
            try:
                self._video_stream_consumer.shutdown()
            except Exception:
                pass
        self._video_stream_consumer = None

        if self._video_stream_overlay_id and self._video_stream_overlay_id in self._overlays:
            self._overlays.pop(self._video_stream_overlay_id, None)
        self._video_stream_overlay_id = None
        self._video_stream_pupil_mask = None

        try:
            if self._controller:
                self._controller.update()
        except Exception:
            pass

    def get_video_stream_status(self) -> dict:
        return {
            "enabled": bool(self._video_stream_thread and self._video_stream_thread.is_alive()),
            "frames": self._video_stream_stats.get("frames", 0),
            "errors": self._video_stream_stats.get("errors", 0),
            "fps": self._video_stream_stats.get("last_fps", 0.0),
        }
    
    # ==================== LCD 暂停/恢复 (用于 Widget 互斥) ====================
    def pause_lcd(self) -> bool:
        """
        暂停 LCD 渲染，释放 LCD 硬件资源
        
        用于 widget_service 获取 LCD 控制权时调用。
        暂停后所有渲染操作将被跳过，直到调用 resume_lcd()。
        
        Returns:
            成功返回 True
        """
        with self._lcd_pause_lock:
            if self._lcd_paused:
                logger.warning("[ENGINE] LCD 已处于暂停状态")
                return True
            
            logger.info("[ENGINE] 开始暂停 LCD (widget 互斥)")
            
            try:
                # 1. 停止自动眨眼
                if self._controller:
                    self._controller.disable_auto_blink()
                
                # 2. 停止眼睛动画
                if self._eye_anim_player:
                    try:
                        self._eye_anim_player.stop()
                    except Exception as e:
                        logger.warning(f"[ENGINE] 停止眼睛动画失败: {e}")
                
                # 3. 停止序列播放器
                for player in self._seq_players.values():
                    if player:
                        try:
                            player.stop()
                        except Exception as e:
                            logger.warning(f"[ENGINE] 停止序列播放器失败: {e}")
                
                # 4. 释放 LCD 驱动
                if self._left_driver:
                    try:
                        self._left_driver.release()
                        logger.info("[ENGINE] 左眼 LCD 已释放")
                    except Exception as e:
                        logger.error(f"[ENGINE] 释放左眼 LCD 失败: {e}")
                
                if self._right_driver:
                    try:
                        self._right_driver.release()
                        logger.info("[ENGINE] 右眼 LCD 已释放")
                    except Exception as e:
                        logger.error(f"[ENGINE] 释放右眼 LCD 失败: {e}")
                
                self._lcd_paused = True
                logger.info("[ENGINE] LCD 已暂停 (widget 可以使用)")
                return True
                
            except Exception as e:
                logger.error(f"[ENGINE] 暂停 LCD 失败: {e}")
                return False
    
    def resume_lcd(self) -> bool:
        """
        恢复 LCD 渲染，重新初始化 LCD 硬件
        
        用于 widget_service 释放 LCD 控制权后调用。
        
        Returns:
            成功返回 True
        """
        with self._lcd_pause_lock:
            if not self._lcd_paused:
                logger.warning("[ENGINE] LCD 未处于暂停状态")
                return True
            
            logger.info("[ENGINE] 开始恢复 LCD")
            
            try:
                use_mock = self._config.use_mock
                
                # 1. 重新初始化 LCD 驱动
                if self._left_driver:
                    if not self._left_driver.init():
                        raise LcdDriverError("左眼 LCD 重新初始化失败")
                    logger.info("[ENGINE] 左眼 LCD 已恢复")
                
                if self._right_driver:
                    if not self._right_driver.init():
                        raise LcdDriverError("右眼 LCD 重新初始化失败")
                    logger.info("[ENGINE] 右眼 LCD 已恢复")
                
                # 2. 重新启用自动眨眼（如果配置了且非被动模式）
                if not self._config.passive_mode and self._config.auto_blink:
                    if self._controller:
                        self._controller.enable_auto_blink(self._config.blink_interval)
                
                # 3. 刷新显示
                if self._controller:
                    try:
                        self._controller.update()
                    except Exception as e:
                        logger.warning(f"[ENGINE] 刷新显示失败: {e}")
                
                self._lcd_paused = False
                logger.info("[ENGINE] LCD 已恢复 (eyeEngine 控制)")
                return True
                
            except Exception as e:
                logger.error(f"[ENGINE] 恢复 LCD 失败: {e}")
                return False
    
    @property
    def is_lcd_paused(self) -> bool:
        """检查 LCD 是否处于暂停状态"""
        return self._lcd_paused
    
    def _check_initialized(self) -> None:
        """检查是否已初始化"""
        if not self._initialized:
            raise NotInitializedError("引擎未初始化，请先调用 init()")
    
    @property
    def controller(self) -> EyeController:
        """获取眼睛控制器"""
        self._check_initialized()
        return self._controller

    def set_iris(self, theme: str, style: str, side: LcdSide = LcdSide.BOTH) -> None:
        """Convenience wrapper — 设置虹膜主题和样式 (委托给 Controller)"""
        self._check_initialized()
        if not self._controller:
            raise NotInitializedError()
        self._controller.set_iris_theme(theme, style, side)
        # 立即同步到动画播放器（如果正在播放）
        if self._eye_anim_player:
            left = self._controller.get_state(LcdSide.LEFT)
            right = self._controller.get_state(LcdSide.RIGHT)
            self._eye_anim_player.set_state(left, right)

    def render(self) -> None:
        """执行一次立即刷新显示"""
        self._check_initialized()
        if self._controller:
            # 通过重新设置当前状态触发刷新
            self._controller.set_state(self._controller.get_state(LcdSide.LEFT), LcdSide.BOTH)

    # ---------------- Baseline & Auto Reset ----------------
    def capture_baseline_state(self):
        """捕获当前显示状态作为复位基线。"""
        if not self._controller:
            return
        try:
            self._baseline_left = self._controller.get_state(LcdSide.LEFT)
            self._baseline_right = self._controller.get_state(LcdSide.RIGHT)
            logger.info("[ENGINE] Captured baseline eye state for auto reset")
        except Exception:
            logger.exception("[ENGINE] capture_baseline_state failed")

    def _cancel_auto_reset_timer(self):
        t = self._auto_reset_timer
        if t and t.is_alive():
            try:
                t.cancel()
            except Exception:
                pass
        self._auto_reset_timer = None

    def _schedule_auto_reset(self, reason: str = ""):
        if not getattr(self._config, 'auto_reset_enabled', False):
            return
        # 如果正在播放新动画则取消旧计时
        self._cancel_auto_reset_timer()
        delay = max(0, int(getattr(self._config, 'auto_reset_delay_ms', 300))) / 1000.0

        def _do_reset():
            try:
                self.reset_to_default(reason=reason)
            except Exception:
                logger.exception("[ENGINE] auto reset failed")

        t = threading.Timer(delay, _do_reset)
        t.daemon = True
        self._auto_reset_timer = t
        t.start()
        logger.info(f"[ENGINE] scheduled auto reset in {delay}s (reason={reason})")

    def _finalize_animation_display(self, reason: str = ""):
        """统一处理动画结束后的收尾刷新，避免硬件残帧（尤其右眼）。"""
        if not self._controller:
            return

        try:
            self._controller.set_animating(False)
        except Exception:
            pass

        try:
            self._controller.update()
            time.sleep(0.01)
            self._controller.update()
            logger.debug(f"[ENGINE] finalize animation display done, reason={reason}")
        except Exception as e:
            logger.warning(f"[ENGINE] finalize animation display update failed, reason={reason}, err={e}")

        try:
            if self._renderer and self._left_driver and self._right_driver:
                left_state = self._controller.get_state(LcdSide.LEFT)
                right_state = self._controller.get_state(LcdSide.RIGHT)

                left_img = self._renderer.render(left_state, LcdSide.LEFT)
                right_img = self._renderer.render(right_state, LcdSide.RIGHT)

                self._left_driver.write(self._renderer.convert_to_rgb888(left_img))
                self._right_driver.write(self._renderer.convert_to_rgb888(right_img))
                logger.debug(f"[ENGINE] direct flush after animation done, reason={reason}")
        except Exception as e:
            logger.warning(f"[ENGINE] direct flush after animation failed, reason={reason}, err={e}")

    def reset_to_default(self, reason: str = "") -> bool:
        """将显示恢复到基线或默认表情。"""
        self._check_initialized()
        if not getattr(self._config, 'auto_reset_enabled', False):
            logger.debug("[ENGINE] auto_reset disabled, skip reset")
            return False

        # 避免在播放中强制复位
        try:
            if self._eye_anim_player and getattr(self._eye_anim_player, 'is_playing', False):
                logger.info("[ENGINE] skip reset: animation still playing")
                return False
        except Exception:
            pass

        # 选择复位策略
        expr = getattr(self._config, 'auto_reset_expression', '') or ''
        try:
            if expr:
                logger.info(f"[ENGINE] auto reset by playing expression='{expr}' reason={reason}")
                self.play_behavior(expr, blocking=False)
            elif self._baseline_left and self._baseline_right:
                logger.info(f"[ENGINE] auto reset to baseline states reason={reason}")

                # 如果 auto_reset_background 为 False，则在复位眼球和眼睑状态的同时，保留当前背景
                if not getattr(self._config, 'auto_reset_background', True):
                    # 获取当前背景状态
                    current_left = self._controller.get_state(LcdSide.LEFT)
                    current_right = self._controller.get_state(LcdSide.RIGHT)

                    # 复制基线并写回当前背景
                    target_left = self._baseline_left.copy()
                    target_left.background_type = current_left.background_type
                    target_left.background_style = current_left.background_style

                    target_right = self._baseline_right.copy()
                    target_right.background_type = current_right.background_type
                    target_right.background_style = current_right.background_style

                    self._controller.set_state(target_left, LcdSide.LEFT)
                    self._controller.set_state(target_right, LcdSide.RIGHT)
                else:
                    # 默认行为：完全恢复到基线（包含背景）
                    self._controller.set_state(self._baseline_left, LcdSide.LEFT)
                    self._controller.set_state(self._baseline_right, LcdSide.RIGHT)

                self._controller.update()
            else:
                logger.info(f"[ENGINE] auto reset using render refresh reason={reason}")
                self.render()
        except Exception:
            logger.exception("[ENGINE] auto reset execution failed")
            return False
        return True

    @property
    def asset_manager(self) -> AssetManager:
        """获取资源管理器"""
        self._check_initialized()
        return self._asset_manager
    
    @property
    def lcd_driver(self) -> ILcdDriver:
        """获取左眼 LCD 驱动 (兼容旧代码)"""
        self._check_initialized()
        return self._left_driver
    
    @property
    def left_driver(self) -> ILcdDriver:
        """获取左眼 LCD 驱动"""
        self._check_initialized()
        return self._left_driver
    
    @property
    def right_driver(self) -> ILcdDriver:
        """获取右眼 LCD 驱动"""
        self._check_initialized()
        return self._right_driver
    
    @property
    def behavior_manager(self) -> Optional[BehaviorManager]:
        return self._behavior_manager

    @property
    def eye_animation_player(self):
        """获取眼睛动画播放器"""
        return self._eye_anim_player
    
    def _sync_player_state(self):
        """将当前 Controller 状态同步到 EyeAnimationPlayer（如果存在）"""
        if not self._eye_anim_player:
            return
        try:
            left = self._controller.get_state(LcdSide.LEFT)
            right = self._controller.get_state(LcdSide.RIGHT)
            self._eye_anim_player.set_state(left, right)
        except Exception:
            pass

    def _play_blink_animation(self, animation_name: str):
        """眨眼回调：播放指定的眨眼动画"""
        # 注意：这里我们使用非阻塞播放，因为自动眨眼不需要等待。
        # play_eye_animation 会自动同步状态并设置 animating 标志。
        try:
            self.play_eye_animation(animation_name, blocking=False)
        except Exception as e:
            logger.error(f"EyeEngine: 播放眨眼动画 {animation_name} 失败: {e}")

    def play_eye_animation(self, category_name: str, animation_name: Optional[str] = None, fps: Optional[int] = None, blocking: bool = True, hold_duration: float = 0.0) -> bool:
        """
        播放动画。

        用法:
            - play_eye_animation("ADMIRING")  # 通过动画名称
            - play_eye_animation("HAPPINESS", "ADMIRING")  # 通过类别+动画名称

        Returns:
            是否成功开始播放
        """
        self._check_initialized()
        if not self._eye_anim_player:
            logger.error("EyeAnimationPlayer 未初始化")
            return False

        # 新动画开始前取消已排定的自动复位
        self._cancel_auto_reset_timer()

        # 如果只传递了一个参数，将其作为动画名称处理
        if animation_name is None:
            animation_obj = self.asset_manager.config_loader.get_animation_by_name(category_name)
        else:
            animation_obj = self.asset_manager.config_loader.get_animation(category_name, animation_name)

        if not animation_obj:
            logger.error(f"找不到动画: {category_name} {animation_name or ''}")
            return False

        # 播放前同步当前状态
        left_state = self.controller.get_state(LcdSide.LEFT)
        right_state = self.controller.get_state(LcdSide.RIGHT)
        self._eye_anim_player.set_state(left_state, right_state)

        # 设置 FPS
        if fps is not None:
            self._eye_anim_player.set_fps(fps)
        else:
            self._eye_anim_player.set_fps(self._config.default_fps)

        # 把 controller 标记为正在播放动画
        if self._controller:
            try:
                self._controller.set_animating(True)
            except Exception:
                pass

        # 播放完成回调逻辑
        # 注意：如果 blocking=True，我们会在 play_animation 返回后处理 hold
        # 如果 blocking=False，我们需要在回调里处理 hold? 不，ZMQ 任务通常用 blocking=True 执行
        
        def _on_animation_complete():
            # 如果是非阻塞调用，回调里清理标志
            if not blocking:
                # 在非阻塞回调里也尝试把播放器最终状态写回 controller（在被动模式下很有用）
                try:
                    player_left, player_right = None, None
                    try:
                        player_left, player_right = self._eye_anim_player.get_state()
                    except Exception:
                        logger.exception("_on_animation_complete: 获取播放器最终状态失败")

                    if (hold_duration > 0 or getattr(self._config, 'passive_mode', False)) and self._controller and player_left is not None:
                        try:
                            self._controller.set_state(player_left, LcdSide.LEFT)
                            self._controller.set_state(player_right, LcdSide.RIGHT)
                            # logger.info("_on_animation_complete: applied final player state to controller (hold/passive)")
                        except Exception:
                            logger.exception("_on_animation_complete: 无法将最终状态写入 controller")

                finally:
                    self._finalize_animation_display(reason="eye_animation_complete_async")
                    # 非阻塞完成后调度自动复位
                    self._schedule_auto_reset(reason="eye_animation_complete_async")
        
        # 真正开始播放
        logger.info(f"play_eye_animation: starting animation {animation_obj.name} blocking={blocking} hold_duration={hold_duration} passive_mode={self._config.passive_mode}")
        logger.debug(f"play_eye_animation: category={category_name}, animation={animation_name}, fps={fps}, blocking={blocking}, hold_duration={hold_duration}")
        started = self._eye_anim_player.play_animation(animation_obj, blocking=blocking, on_complete=_on_animation_complete)
        
        if started:
            if blocking:
                # 只有在阻塞模式下，我们才能在这里安全地等待保持时长
                # 在播放完成后，记录播放器的最终状态并在被动或有 hold_duration 时把最终状态写回 Controller
                try:
                    player_left, player_right = self._eye_anim_player.get_state()
                    logger.info(f"play_eye_animation: animation '{animation_obj.name}' completed, player final state left.iris_x={player_left.iris_x} right.iris_x={player_right.iris_x}")
                    # 如果需要保持（hold_duration>0）或处于被动模式，则把最终状态设置到 Controller，避免恢复到默认
                    if hold_duration > 0 or getattr(self._config, 'passive_mode', False):
                        try:
                            self._controller.set_state(player_left, LcdSide.LEFT)
                            self._controller.set_state(player_right, LcdSide.RIGHT)
                            logger.info(f"play_eye_animation: applied final player state to controller (hold/passive)")
                        except Exception:
                            logger.exception("play_eye_animation: 无法将最终状态写入 controller")
                except Exception:
                    logger.exception("play_eye_animation: 获取播放器最终状态失败")

                if hold_duration > 0:
                    logger.debug(f"Animation finished, holding state for {hold_duration}s")
                    time.sleep(hold_duration)
                
                # 阻塞完成后清理标志
                self._finalize_animation_display(reason="eye_animation_complete_blocking")
                # 阻塞模式完成后调度自动复位
                self._schedule_auto_reset(reason="eye_animation_complete_blocking")
        else:
            # 启动失败，清理标志
            if self._controller:
                try:
                    self._controller.set_animating(False)
                except Exception:
                    pass
                    
        return started

    def play_eye_animation_by_id(self, anim_id: int, fps: Optional[int] = None, blocking: bool = True, hold_duration: float = 0.0) -> bool:
        """通过 ID 播放动画"""
        self._check_initialized()
        animation_obj = self.asset_manager.config_loader.get_animation_by_id(anim_id)
        if not animation_obj:
            logger.error(f"找不到动画 ID: {anim_id}")
            return False
            
        # 委托给上面的通用方法
        return self.play_eye_animation(animation_obj.category_name, animation_obj.name, fps=fps, blocking=blocking, hold_duration=hold_duration)

    def play_behavior(self, behavior_name: str, level: int = 1, fps: Optional[int] = None, blocking: bool = True, hold_duration: float = 0.0) -> bool:

        # 否则通过动画名称播放（忽略 category_name 或用于日志）
        animation_obj = self.asset_manager.config_loader.get_animation_by_name(animation_name)
        if not animation_obj:
            logger.error(f"找不到动画: {animation_name}")
            if self._controller:
                try:
                    self._controller.set_animating(False)
                except Exception:
                    pass
            return False
        started = self._eye_anim_player.play_animation(animation_obj, blocking=blocking, on_complete=_on_animation_complete)
        if not started:
            if self._controller:
                try:
                    self._controller.set_animating(False)
                except Exception:
                    pass
        return started

    def play_eye_animation_by_id(self, anim_id: int, fps: Optional[int] = None, blocking: bool = True) -> bool:
        """
        通过动画 ID 播放动画（ID 来自 eyeanimations.xml 中的 anim_id）
        """
        self._check_initialized()
        if not self._eye_anim_player:
            logger.error("EyeAnimationPlayer 未初始化")
            return False

        # 同步当前 state
        left_state = self.controller.get_state(LcdSide.LEFT)
        right_state = self.controller.get_state(LcdSide.RIGHT)
        self._eye_anim_player.set_state(left_state, right_state)

        # 应用 FPS 限制
        if fps is not None:
            self._eye_anim_player.set_fps(fps)
        else:
            self._eye_anim_player.set_fps(self._config.default_fps)

        # 把 controller 标记为正在播放动画
        if self._controller:
            try:
                self._controller.set_animating(True)
            except Exception:
                pass

        def _on_animation_complete():
            try:
                if self._controller:
                    self._controller.set_animating(False)
                    try:
                        self._controller.update()
                    except Exception:
                        pass
            except Exception:
                pass

        started = self._eye_anim_player.play_by_id(anim_id, blocking=blocking, on_complete=_on_animation_complete)
        if not started:
            if self._controller:
                try:
                    self._controller.set_animating(False)
                except Exception:
                    pass
        return started

    def play_behavior(self, behavior_name: str, level: int = 1, fps: Optional[int] = None, blocking: bool = True, hold_duration: float = 0.0) -> bool:
        """
        播放 behavior (来自 animationlist.xml)
        
        Args:
            behavior_name: 行为名称 (如 "ANIMATION_HAPPY")
            level: 等级
            fps: 帧率
            blocking: 是否阻塞直到动画播放结束
            hold_duration: 保持时长
            
        Returns:
            是否成功启动播放
        """
        if not self._initialized:
            raise NotInitializedError()
            
        if not self._behavior_manager:
            logger.error("BehaviorManager 未初始化")
            return False

        anim_info = self._behavior_manager.get_random_animation_for_behavior(behavior_name, level)
        if anim_info:
            category, animation = anim_info
            logger.info(f"Playing behavior {behavior_name} (level {level}) -> {category}:{animation}")
            return self.play_eye_animation(category, animation, fps=fps, blocking=blocking, hold_duration=hold_duration)
        else:
            logger.warning(f"No eye animation found for behavior {behavior_name}")
            return False

    def stop_animation(self):
        """
        停止当前动画
        """
        self._check_initialized()
        if self._eye_anim_player:
            self._eye_anim_player.stop()
            # 清理 animating 标志
            self._finalize_animation_display(reason="stop_animation")

    # -------------------- Compatibility / Convenience API --------------------
    @property
    def is_initialized(self) -> bool:
        """兼容属性：引擎是否已初始化"""
        return bool(self._initialized)

    def __enter__(self):
        """上下文管理器支持（`with EyeEngine() as engine:`）"""
        self.init()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.release()

    def get_animation_interface(self):
        """返回与 animation_system 兼容的接口实例"""
        from .integration.animation_interface import AnimationSystemInterface
        return AnimationSystemInterface(self)

    # ---------------- Overlay sequence API ----------------
    def play_sequence_animations(self, sequence: str, side: LcdSide = LcdSide.BOTH, loop: bool = False, loop_count: int = None, fps: Optional[int] = None, speed: float = 1.0, clear_time: int = 0, exclusive: bool = False) -> Optional[str]:
        """
        在当前渲染之上叠加播放一个 .seq 序列（最小实现：创建 SeqPlayer 并以非阻塞方式播放）
        """
        self._check_initialized()
        import uuid
        overlay_id = str(uuid.uuid4())[:8]
        
        # 解析文件路径
        if not sequence.endswith('.seq'):
            filename = f"{sequence}.seq"
        else:
            filename = sequence
            
        full_path = Path(self._config.images_path) / "animations" / filename
        if not full_path.exists():
            logger.error(f"play_sequence_animations: File not found: {full_path}")
            return None

        # 默认帧率
        if fps is None:
            fps = self._config.default_fps_seq

        players = []
        try:
            # 播放器结束回调逻辑：支持 clear_time 自动清理
            # 使用 Event 确保不论左右 player 谁先完成，完成逻辑只执行一次
            completed_event = threading.Event()
            def on_player_complete():
                if completed_event.is_set():
                    return
                completed_event.set()
                # ★ 新增：seq 播放完成时立即发布 overlay.completed 事件（只发布一次）
                if self._on_overlay_event:
                    try:
                        self._on_overlay_event('overlay.completed', {'overlay_id': overlay_id})
                    except Exception as e:
                        logger.warning(f"play_sequence_animations: 发布 overlay.completed 事件失败: {e}")

                if clear_time > 0:
                    def delayed_clear():
                        time.sleep(clear_time / 1000.0)
                        logger.info(f"play_sequence_animations: clearing overlay {overlay_id} after {clear_time}ms")
                        self.stop_overlay_sequence(overlay_id)
                    threading.Thread(target=delayed_clear, daemon=True).start()
                else:
                    logger.info(f"play_sequence_animations: overlay {overlay_id} finished (no clear_time)")

            if side == LcdSide.LEFT or side == LcdSide.BOTH:
                p = SeqPlayer(self._left_driver)
                if not p.load(str(full_path), side=LcdSide.LEFT):
                    logger.error(f"play_sequence_animations: load failed for LEFT: {full_path}")
                else:
                    p.set_frame_consumer(self._on_seq_frame)
                    p.set_on_complete(on_player_complete)
                    p.play(loop=loop, fps=fps, loop_count=loop_count)
                    players.append(p)
            
            if side == LcdSide.RIGHT or side == LcdSide.BOTH:
                p = SeqPlayer(self._right_driver)
                if not p.load(str(full_path), side=LcdSide.RIGHT):
                    logger.error(f"play_sequence_animations: load failed for RIGHT: {full_path}")
                else:
                    p.set_frame_consumer(self._on_seq_frame)
                    p.set_on_complete(on_player_complete)
                    p.play(loop=loop, fps=fps, loop_count=loop_count)
                    players.append(p)

            if not players:
                return None

            # store overlay info
            self._overlays[overlay_id] = {
                'overlay_id': overlay_id,
                'players': players,
                'side': side,
                'sequence': sequence,
                'loop': loop,
                'fps': fps,
                'speed': speed,
                'clear_time': clear_time,
                'exclusive': exclusive,
                'latest_frame_left': None,
                'latest_frame_right': None,
                'frame_lock': threading.Lock()
            }
            
            logger.info(f"play_sequence_animations: started overlay {overlay_id} for {sequence} on {side}, clear_time={clear_time}, exclusive={exclusive}")
            return overlay_id

        except Exception as e:
            logger.exception(f"play_sequence_animations failed: {e}")
            return None

    def stop_overlay_sequence(self, overlay_id: str) -> bool:
        """停止指定 overlay（overlay_id）并清理"""
        self._check_initialized()
        if overlay_id not in self._overlays:
            logger.warning(f"stop_overlay_sequence: unknown overlay_id {overlay_id}")
            return False

        info = self._overlays.pop(overlay_id)
        try:
            players = info.get('players') or []
            for player in players:
                try:
                    player.stop()
                except Exception:
                    logger.exception("stop_overlay_sequence: failed to stop a player")
            logger.info(f"Overlay stopped: {overlay_id}")
            # ★ 新增：发布 overlay.stopped 事件
            if self._on_overlay_event:
                try:
                    self._on_overlay_event('overlay.stopped', {'overlay_id': overlay_id})
                except Exception as e:
                    logger.warning(f"stop_overlay_sequence: 发布 overlay.stopped 事件失败: {e}")
            # 恢复 controller 显示（如果需要）
            try:
                self._controller.update()
            except Exception:
                pass
            # 兜底：确保硬件上覆盖被清除——直接渲染并强制写回两侧屏幕
            try:
                try:
                    img_l = self._renderer.render(self._left_state, LcdSide.LEFT)
                    data_l = self._renderer.convert_to_rgb888(img_l)
                    self._left_driver.write(data_l)
                except Exception:
                    logger.debug("stop_overlay_sequence: 强制刷新左眼失败或不需要")
                try:
                    img_r = self._renderer.render(self._right_state, LcdSide.RIGHT)
                    data_r = self._renderer.convert_to_rgb888(img_r)
                    self._right_driver.write(data_r)
                except Exception:
                    logger.debug("stop_overlay_sequence: 强制刷新右眼失败或不需要")
            except Exception:
                logger.exception("stop_overlay_sequence: 强制写回硬件失败")
            return True
        except Exception:
            logger.exception(f"stop_overlay_sequence: failed to stop {overlay_id}")
            return False

    def _on_seq_frame(self, side: LcdSide, frame_np: 'np.ndarray') -> None:
        """
        SeqPlayer 的 frame_consumer 回调，用于把每帧存入 overlay registry 的 latest_frame 字段。
        """
        try:
            # 找到所有匹配 side 的 overlays（可能存在多个 overlay 叠加）并更新 latest_frame
            for oid, info in list(self._overlays.items()):
                if info.get('side') == side or info.get('side') == LcdSide.BOTH:
                    lock = info.get('frame_lock')
                    if lock:
                        with lock:
                            # store a copy to avoid later修改影响播放器
                            if side == LcdSide.LEFT:
                                info['latest_frame_left'] = np.array(frame_np, copy=True)
                            elif side == LcdSide.RIGHT:
                                info['latest_frame_right'] = np.array(frame_np, copy=True)
                            else:
                                # BOTH: store to both
                                info['latest_frame_left'] = np.array(frame_np, copy=True)
                                info['latest_frame_right'] = np.array(frame_np, copy=True)
                            
            # trigger a controller update so the composed frame is written
            try:
                if self._controller:
                    self._controller.update()
            except Exception:
                pass
        except Exception:
            logger.exception("_on_seq_frame: failed to store overlay frame")

    def _get_active_overlays(self):
        """Return list of overlay info dicts that are currently active."""
        # 只返回未被 stop 的 overlay，防止残帧叠加
        overlays = []
        for info in self._overlays.values():
            # 如果 overlay dict 已经被 stop/清理，则不返回
            if info.get('_stop_flag', False):
                continue
            overlays.append(info)
        return overlays

    def fill_color(self, r: int, g: int, b: int, side: LcdSide = LcdSide.BOTH) -> None:
        """填充纯色到显示器（兼容旧 API）"""
        if side == LcdSide.LEFT or side == LcdSide.BOTH:
            try:
                self._left_driver.fill_color(r, g, b)
            except Exception:
                logger.exception("fill_color left failed")
        if side == LcdSide.RIGHT or side == LcdSide.BOTH:
            try:
                self._right_driver.fill_color(r, g, b)
            except Exception:
                logger.exception("fill_color right failed")

    def play_sequence(self, name: str, side: LcdSide = LcdSide.BOTH, loop: bool = False, fps: Optional[int] = None):
        """播放 .seq 序列文件（name 可以是文件名不含扩展）

        Returns:
            SeqPlayer 或 None
        """
        self._check_initialized()

        # 如果没有指定 fps，使用配置中的序列文件默认帧率
        if fps is None:
            fps = self._config.default_fps_seq

        # 解析文件路径
        seq_name = name
        seq_path = Path(self._config.images_path) / "animations"
        candidate = Path(seq_name)
        if candidate.is_file():
            filepath = candidate
        else:
            filepath = seq_path / f"{seq_name}.seq"
            if not filepath.exists():
                # 也尝试直接传入带扩展名的名字
                filepath = seq_path / seq_name
                if not filepath.exists():
                    logger.error(f"play_sequence: 找不到文件: {seq_name}")
                    return None

        players = []
        if side == LcdSide.LEFT or side == LcdSide.BOTH:
            left = self._seq_players.get(LcdSide.LEFT)
            if left:
                if not left.load(str(filepath), side=LcdSide.LEFT):
                    logger.error(f"SeqPlayer load failed for LEFT: {filepath}")
                else:
                    left.play(loop=loop, fps=fps)
                    players.append(left)
        if side == LcdSide.RIGHT or side == LcdSide.BOTH:
            right = self._seq_players.get(LcdSide.RIGHT)
            if right:
                if not right.load(str(filepath), side=LcdSide.RIGHT):
                    logger.error(f"SeqPlayer load failed for RIGHT: {filepath}")
                else:
                    right.play(loop=loop, fps=fps)
                    players.append(right)

        if players:
            return players[0]
        return None

    def stop_sequence(self) -> None:
        """停止当前正在播放的 seq（如果有）"""
        self._check_initialized()
        left = self._seq_players.get(LcdSide.LEFT)
        right = self._seq_players.get(LcdSide.RIGHT)
        if left:
            left.stop()
        if right:
            right.stop()