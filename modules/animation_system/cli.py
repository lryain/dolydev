"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
Doly 动画系统命令行工具

用法:
    python3 cli.py list
    python3 cli.py run <category> <level>
    python3 cli.py run_file <file_path>
"""

import asyncio
import sys
import logging
import argparse
from pathlib import Path
from typing import Optional, Dict, Any

# 添加项目根目录到 sys.path
project_root = Path(__file__).parent.parent.parent
if str(project_root) not in sys.path:
    sys.path.insert(0, str(project_root))

from modules.animation_system.factory import create_real_hardware
from modules.animation_system.animation_manager import AnimationManager
from modules.animation_system.executor import AnimationExecutor
import zmq
import json
import yaml
from pathlib import Path

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger("AnimationCLI")


def _load_eye_endpoints():
    """从 system.yaml 读取 EyeEngine ZMQ 端点，默认使用本地 ipc。"""
    cfg_path = Path(__file__).parent.parent.parent / 'config' / 'system.yaml'
    cmd_endpoint = 'ipc:///tmp/doly_eye_cmd.sock'
    event_endpoint = 'ipc:///tmp/doly_eye_event.sock'
    try:
        cfg = yaml.safe_load(cfg_path.read_text())
        zmq_cfg = cfg.get('zmq_service', {}) or {}
        cmd_endpoint = zmq_cfg.get('command_endpoint', cmd_endpoint)
        event_endpoint = zmq_cfg.get('event_endpoint', event_endpoint)
    except Exception:
        logger.debug("未读取到自定义 ZMQ 端点，使用默认 ipc")
    return cmd_endpoint, event_endpoint


def send_eye_command(cmd: Dict[str, Any], timeout_ms: int = 5000) -> Dict[str, Any]:
    """发送 ZMQ 命令到 EyeEngine，返回响应（失败时返回 error 字段）。"""
    cmd_endpoint, _ = _load_eye_endpoints()
    ctx = zmq.Context()
    sock = ctx.socket(zmq.REQ)
    sock.connect(cmd_endpoint)
    sock.setsockopt(zmq.LINGER, 0)
    sock.setsockopt(zmq.RCVTIMEO, timeout_ms)
    try:
        logger.info(f"[ZMQ] send -> {cmd_endpoint}: {json.dumps(cmd, ensure_ascii=False)}")
        sock.send_json(cmd)
        resp = sock.recv_json()
        logger.info(f"[ZMQ] recv <- {json.dumps(resp, ensure_ascii=False)}")
        return resp
    except zmq.error.Again:
        return {"success": False, "error": "命令超时"}
    except Exception as e:
        return {"success": False, "error": str(e)}
    finally:
        sock.close()
        ctx.term()

class AnimationCLI:
    def __init__(self, anim_path: str = "/home/pi/dolydev/assets/config/animations"):
        self.anim_path = anim_path
        self.interfaces = create_real_hardware()
        self.manager = AnimationManager(self.anim_path)
        self.executor = AnimationExecutor(self.interfaces)
        
    def load(self):
        self.manager.load_animations()

    def list_behaviors(self):
        print("\n" + "="*50)
        print(f"{'Category':<30} | {'Levels'}")
        print("-" * 50)
        for cat_name, category in sorted(self.manager.categories.items()):
            levels = ", ".join(map(str, sorted(category.animations.keys())))
            print(f"{cat_name:<30} | {levels}")
        print("="*50 + "\n")

    async def run_behavior(self, category_name: str, level: int = 1, no_priority: bool = False):
        print(f"🚀 正在请求: {category_name} (Level {level})")
        orig_priority = None
        # Optionally disable priority on EyeEngine for this run
        if no_priority and self.interfaces and self.interfaces.eye:
            try:
                orig_priority = self.interfaces.eye.priority_enabled
                # call synchronously in thread to avoid blocking event loop
                import asyncio as _asyncio
                ok = await _asyncio.to_thread(self.interfaces.eye.set_priority_enabled, False)
                if ok:
                    logger.info("[AnimationCLI] Temporarily disabled EyeEngine priority system for this run")
                else:
                    logger.warning("[AnimationCLI] Failed to disable EyeEngine priority system; proceeding with original setting")
            except Exception as e:
                logger.warning(f"[AnimationCLI] Error disabling priority: {e}")

        try:
            # 使用管理器获取动画，它会处理随机选择
            blocks = self.manager.get_animation(category_name, level, random_select=True)

            if not blocks:
                print(f"❌ 分类 {category_name} 在等级 {level} 下没有动画或获取失败")
                return

            await self.executor.execute(blocks)
            print("✅ 执行完成")
        finally:
            # restore priority setting
            if no_priority and orig_priority is not None and self.interfaces and self.interfaces.eye:
                try:
                    import asyncio as _asyncio
                    await _asyncio.to_thread(self.interfaces.eye.set_priority_enabled, orig_priority)
                    logger.info("[AnimationCLI] Restored EyeEngine priority system setting")
                except Exception as e:
                    logger.warning(f"[AnimationCLI] Failed to restore EyeEngine priority setting: {e}")

    async def run_all_behaviors(self, category_name: str, level: int = None, no_priority: bool = False):
        """
        执行指定分类的全部行为。
        如果 level 指定，仅执行该等级下的所有预设（按文件顺序）。
        如果 level 为 None，则按等级顺序执行该分类下的所有预设。
        """
        orig_priority = None
        if no_priority and self.interfaces and self.interfaces.eye:
            try:
                import asyncio as _asyncio
                orig_priority = self.interfaces.eye.priority_enabled
                ok = await _asyncio.to_thread(self.interfaces.eye.set_priority_enabled, False)
                if ok:
                    logger.info("[AnimationCLI] Temporarily disabled EyeEngine priority system for this run_all")
            except Exception as e:
                logger.warning(f"[AnimationCLI] Error disabling priority: {e}")

        try:
            category = self.manager.categories.get(category_name)
            if not category:
                print(f"❌ 找不到分类: {category_name}")
                return

            # 运行指定等级的全部变体
            if level is not None:
                file_paths = category.get_animations(level)
                if not file_paths:
                    print(f"❌ 分类 {category_name} 在等级 {level} 下没有动画")
                    return

                for file_path in file_paths:
                    print(f"🚀 正在执行: {category_name} (Level {level}) -> {file_path}")
                    try:
                        blocks = self.manager._load_animation_file(file_path)
                        await self.executor.execute(blocks)
                    except Exception as e:
                        logger.error(f"执行 {file_path} 时发生错误: {e}")
                        continue
                    print(f"✅ 完成: {file_path}")
                return

            # 运行所有等级的预设，按等级升序
            for lvl in sorted(category.animations.keys()):
                file_paths = category.get_animations(lvl)
                for file_path in file_paths:
                    print(f"🚀 正在执行: {category_name} (Level {lvl}) -> {file_path}")
                    try:
                        blocks = self.manager._load_animation_file(file_path)
                        await self.executor.execute(blocks)
                    except Exception as e:
                        logger.error(f"执行 {file_path} 时发生错误: {e}")
                        continue
                    print(f"✅ 完成: {file_path}")
        finally:
            if no_priority and orig_priority is not None and self.interfaces and self.interfaces.eye:
                try:
                    import asyncio as _asyncio
                    await _asyncio.to_thread(self.interfaces.eye.set_priority_enabled, orig_priority)
                    logger.info("[AnimationCLI] Restored EyeEngine priority system setting")
                except Exception as e:
                    logger.warning(f"[AnimationCLI] Failed to restore EyeEngine priority setting: {e}")

    async def run_file(self, file_path: str, no_priority: bool = False):
        full_path = Path(self.anim_path) / file_path
        if not full_path.exists():
            # 兼容直接传入绝对路径或 assets/config/animations 路径
            full_path = Path(file_path)
        if not full_path.exists():
            # 兼容老路径
            alt_path = Path("/home/pi/dolydev/assets/config/animations") / file_path
            if alt_path.exists():
                full_path = alt_path
            else:
                print(f"❌ 找不到文件: {full_path}")
                return

        print(f"🚀 正在执行文件: {full_path}")
        blocks = self.manager.parser.parse_animation(str(full_path))

        orig_priority = None
        if no_priority and self.interfaces and self.interfaces.eye:
            try:
                import asyncio as _asyncio
                orig_priority = self.interfaces.eye.priority_enabled
                ok = await _asyncio.to_thread(self.interfaces.eye.set_priority_enabled, False)
                if ok:
                    logger.info("[AnimationCLI] Temporarily disabled EyeEngine priority system for this run_file")
            except Exception as e:
                logger.warning(f"[AnimationCLI] Error disabling priority: {e}")

        try:
            await self.executor.execute(blocks)
            print("✅ 执行完成")
        except KeyboardInterrupt:
            print("⚠️ 检测到 Ctrl+C，正在终止所有动画任务...")
            self.executor.cancel_all_tasks()
            print("✅ 已请求终止所有子任务，主进程即将退出")
            raise
        finally:
            if no_priority and orig_priority is not None and self.interfaces and self.interfaces.eye:
                try:
                    import asyncio as _asyncio
                    await _asyncio.to_thread(self.interfaces.eye.set_priority_enabled, orig_priority)
                    logger.info("[AnimationCLI] Restored EyeEngine priority system setting")
                except Exception as e:
                    logger.warning(f"[AnimationCLI] Failed to restore EyeEngine priority setting: {e}")

    async def set_eyeengine_priority(self, enable: bool):
        """
        启用或禁用 EyeEngine 的优先级系统
        """
        print(f"🔧 {'启用' if enable else '禁用'} EyeEngine 优先级系统")
        cfg_path = Path(__file__).parent.parent.parent / 'config' / 'system.yaml'
        cmd_endpoint = 'ipc:///tmp/doly_eye_cmd.sock'
        try:
            import yaml
            cfg = yaml.safe_load(cfg_path.read_text())
            cmd_endpoint = cfg.get('zmq_service', {}).get('command_endpoint', cmd_endpoint)
        except Exception:
            pass

        ctx = zmq.Context()
        sock = ctx.socket(zmq.REQ)
        sock.connect(cmd_endpoint)
        cmd = {
            'action': 'set_priority',
            'enable': enable
        }
        sock.send_json(cmd)
        resp = sock.recv_json()
        print(json.dumps(resp, indent=2, ensure_ascii=False))
        sock.close()
        ctx.term()

    async def test_motors(self):
        print("🧪 开始电机测试...")
        
        print("1. 前进 0.2 速度, 1秒")
        await self.interfaces.drive.motor_forward(0.2, 1.0)
        await asyncio.sleep(1.5)
        
        print("2. 后退 0.2 速度, 1秒")
        await self.interfaces.drive.motor_backward(0.2, 1.0)
        await asyncio.sleep(1.5)
        
        print("3. 左转 0.2 速度, 1秒")
        await self.interfaces.drive.motor_turn_left(0.2, 1.0)
        await asyncio.sleep(1.5)
        
        print("4. 右转 0.2 速度, 1秒")
        await self.interfaces.drive.motor_turn_right(0.2, 1.0)
        await asyncio.sleep(1.5)
        
        print("5. 编码器脉冲测试 (100 脉冲)")
        await self.interfaces.drive.move_pulses(100, 0.2)
        await asyncio.sleep(2.0)
        
        print("6. 停止")
        await self.interfaces.drive.stop()
        print("✅ 电机测试完成")

async def main():
    parser = argparse.ArgumentParser(description="Doly Animation System CLI")
    subparsers = parser.add_subparsers(dest="command", help="Commands")
    
    # List command
    subparsers.add_parser("list", help="List all available animation categories and levels")
    
    # Test motors command
    subparsers.add_parser("test_motors", help="Test motor functions (forward, backward, pulses, etc.)")
    
    # Run command
    run_parser = subparsers.add_parser("run", help="Run an animation by category and level")
    run_parser.add_argument("category", help="Animation category name")
    run_parser.add_argument("level", type=int, nargs="?", default=None, help="Animation level (default 1 when omitted unless --all)")
    run_parser.add_argument("--all", action="store_true", help="Execute all animations in the category. If level specified, run all variants at that level; otherwise run across all levels.")
    run_parser.add_argument("--no-priority", action="store_true", help="Temporarily disable EyeEngine priority system while running this animation")
    
    # Run file command
    file_parser = subparsers.add_parser("run_file", help="Run an animation from a specific XML file")
    file_parser.add_argument("path", help="Path to the XML file")
    file_parser.add_argument("--no-priority", action="store_true", help="Temporarily disable EyeEngine priority system while running this animation file")

    # Run sprite animation file command
    sprite_file_parser = subparsers.add_parser("run_sprite_file", help="Run a SpriteAnimation from a specific XML file")
    sprite_file_parser.add_argument("path", help="Path to the XML file containing <SpriteAnimation>")
    sprite_file_parser.add_argument("--no-priority", action="store_true", help="Temporarily disable EyeEngine priority system while running this sprite animation file")

    # Overlay play/stop commands (for testing EyeEngine overlay API via ZMQ)
    overlay_play = subparsers.add_parser("overlay_play", help="Start an overlay .seq on EyeEngine via ZMQ")
    overlay_play.add_argument("sequence", help="Sequence name or path (without .seq)")
    overlay_play.add_argument("--side", choices=["LEFT","RIGHT","BOTH"], default="BOTH")
    overlay_play.add_argument("--loop", action="store_true")
    overlay_play.add_argument("--fps", type=int, default=None)
    overlay_play.add_argument("--speed", type=float, default=1.0)
    overlay_play.add_argument("--delay-ms", dest="delay_ms", type=int, default=0, help="Delay before start (ms)")

    overlay_stop = subparsers.add_parser("overlay_stop", help="Stop an overlay by overlay_id via ZMQ")
    overlay_stop.add_argument("overlay_id", help="overlay id returned by overlay_play")

    # EyeEngine ZMQ direct commands
    subparsers.add_parser("eye_ping", help="Ping EyeEngine service")
    subparsers.add_parser("eye_status", help="Get EyeEngine status")

    eye_list = subparsers.add_parser("eye_list", help="List categories/behaviors/iris/backgrounds")
    eye_list.add_argument("--all", action="store_true", help="List all (default)")
    eye_list.add_argument("--categories", action="store_true")
    eye_list.add_argument("--behaviors", action="store_true")
    eye_list.add_argument("--iris", action="store_true")
    eye_list.add_argument("--backgrounds", action="store_true")

    eye_set_brightness = subparsers.add_parser("eye_set_brightness", help="Set brightness")
    eye_set_brightness.add_argument("level", type=int)
    eye_set_brightness.add_argument("--side", choices=["LEFT","RIGHT","BOTH"], default="BOTH")
    eye_set_brightness.add_argument("--priority", type=int, default=None)

    eye_set_background = subparsers.add_parser("eye_set_background", help="Set background style")
    eye_set_background.add_argument("type", choices=["COLOR","IMAGE"], help="Background type")
    eye_set_background.add_argument("style", help="Background style")
    eye_set_background.add_argument("--priority", type=int, default=None)

    eye_set_iris = subparsers.add_parser("eye_set_iris", help="Set iris theme/style")
    eye_set_iris.add_argument("theme")
    eye_set_iris.add_argument("style")
    eye_set_iris.add_argument("--side", choices=["LEFT","RIGHT","BOTH"], default="BOTH")
    eye_set_iris.add_argument("--priority", type=int, default=None)

    eye_play_anim = subparsers.add_parser("eye_play_animation", help="Play eye animation by name or id")
    eye_play_anim.add_argument("animation", help="Animation name or category:anim", nargs="?")
    eye_play_anim.add_argument("--id", type=int, default=None, help="Animation id (alternative to name)")
    eye_play_anim.add_argument("--fps", type=int, default=None)
    eye_play_anim.add_argument("--hold", dest="hold_duration", type=float, default=0.0, help="Hold duration after finish (seconds)")
    eye_play_anim.add_argument("--priority", type=int, default=None)

    eye_play_behavior = subparsers.add_parser("eye_play_behavior", help="Play behavior (animationlist.xml)")
    eye_play_behavior.add_argument("behavior")
    eye_play_behavior.add_argument("--level", type=int, default=1)
    eye_play_behavior.add_argument("--fps", type=int, default=None)
    eye_play_behavior.add_argument("--hold", dest="hold_duration", type=float, default=0.0)
    eye_play_behavior.add_argument("--priority", type=int, default=None)

    eye_play_category = subparsers.add_parser("eye_play_category", help="Play all animations in a category")
    eye_play_category.add_argument("category")
    eye_play_category.add_argument("--play-all", action="store_true", help="Play all animations in the category")
    eye_play_category.add_argument("--fps", type=int, default=None)
    eye_play_category.add_argument("--priority", type=int, default=None)

    eye_play_seq = subparsers.add_parser("eye_play_seq", help="Play overlay sequence or sprite via EyeEngine")
    eye_play_seq.add_argument("sequence", help="Sequence name or sprite name")
    eye_play_seq.add_argument("--sprite", action="store_true", help="Treat sequence as sprite name")
    eye_play_seq.add_argument("--side", choices=["LEFT","RIGHT","BOTH"], default="BOTH")
    eye_play_seq.add_argument("--loop", action="store_true")
    eye_play_seq.add_argument("--fps", type=int, default=None)
    eye_play_seq.add_argument("--speed", type=float, default=1.0)
    eye_play_seq.add_argument("--delay-ms", dest="delay_ms", type=int, default=0)
    eye_play_seq.add_argument("--x", type=int, default=120, help="overlay x")
    eye_play_seq.add_argument("--y", type=int, default=120, help="overlay y")
    eye_play_seq.add_argument("--scale", type=float, default=1.0)
    eye_play_seq.add_argument("--rotation", type=float, default=0.0)
    eye_play_seq.add_argument("--duration-ms", dest="duration_ms", type=int, default=None)
    eye_play_seq.add_argument("--priority", type=int, default=None)

    eye_play_sprite = subparsers.add_parser("eye_play_sprite", help="Play SpriteAnimation via EyeEngine")
    eye_play_sprite.add_argument("category")
    eye_play_sprite.add_argument("animation")
    eye_play_sprite.add_argument("--start", type=int, default=0)
    eye_play_sprite.add_argument("--priority", type=int, default=None)

    subparsers.add_parser("eye_stop", help="Stop current animation task")

    eye_set_priority = subparsers.add_parser("eye_set_priority", help="Enable/disable priority system")
    eye_set_priority.add_argument("--enable", action="store_true", help="Enable priority system")
    eye_set_priority.add_argument("--disable", action="store_true", help="Disable priority system")

    subparsers.add_parser("eye_priority_status", help="Show EyeEngine priority snapshot")
    
    args = parser.parse_args()
    
    cli = AnimationCLI()
    cli.load()
    
    if args.command == "list":
        cli.list_behaviors()
    elif args.command == "test_motors":
        await cli.test_motors()
    elif args.command == "run":
        if getattr(args, "all", False):
            await cli.run_all_behaviors(args.category, args.level, no_priority=args.no_priority)
        else:
            level = args.level if args.level is not None else 1
            await cli.run_behavior(args.category, level, no_priority=args.no_priority)
    elif args.command == "run_file":
        await cli.run_file(args.path, no_priority=args.no_priority)
    elif args.command == "run_sprite_file":
        # 新增：专门解析 <SpriteAnimation>，按事件参数驱动 overlay 动画
        from xml.etree import ElementTree as ET
        xml_path = Path(args.path)
        if not xml_path.exists():
            print(f"❌ 找不到文件: {xml_path}")
            return
        tree = ET.parse(str(xml_path))
        root = tree.getroot()
        sprite_anim = None
        for elem in root.iter():
            tag = elem.tag.split('}')[-1] if '}' in elem.tag else elem.tag
            if tag == 'SpriteAnimation':
                sprite_anim = elem
                break
        if sprite_anim is None:
            print(f"❌ 未找到 <SpriteAnimation> 元素")
            return
        sprite_name = sprite_anim.get('Sprites') or sprite_anim.get('Name')
        # 只支持单侧测试，优先 Left
        side_elem = sprite_anim.find('AnimationSide[@Side="Left"]') or sprite_anim.find('AnimationSide')
        if side_elem is None:
            print(f"❌ 未找到 <AnimationSide> 元素")
            return
        # 解析事件序列
        events = []
        for event in side_elem.findall('Event'):
            params = {k: event.get(k) for k in event.keys()}
            events.append(params)
        # 按事件顺序驱动 overlay 动画
        import asyncio
        async def play_sprite_events():
            overlay_id = None
            for idx, ev in enumerate(events):
                x = int(ev.get('X', 120))
                y = int(ev.get('Y', 120))
                scale = float(ev.get('ScaleX', 1.0))
                rotation = float(ev.get('RotateAngle', 0.0))
                duration = int(ev.get('TimeMs', 500))
                wait = int(ev.get('Wait', 0))
                # 发送 ZMQ play_sequence_animations 命令
                ctx = zmq.Context()
                sock = ctx.socket(zmq.REQ)
                sock.connect('ipc:///tmp/doly_eye_cmd.sock')
                cmd = {
                    'action': 'play_sequence_animations',
                    'sprite': sprite_name,
                    'side': 'LEFT',
                    'x': x,
                    'y': y,
                    'scale': scale,
                    'rotation': rotation,
                    'duration_ms': duration,
                    'loop': False,
                    'fps': 15,
                    'speed': 1.0,
                    'delay_ms': 0
                }
                sock.send_json(cmd)
                resp = sock.recv_json()
                print(f"事件{idx+1}: {cmd} -> {resp}")
                sock.close()
                ctx.term()
                # 等待事件持续时间+wait
                await asyncio.sleep((duration+wait)/1000.0)
        await play_sprite_events()
    elif args.command == "overlay_play":
        # send ZMQ command to EyeEngine
        # try to read endpoint from config file
        cfg_path = Path(__file__).parent.parent.parent / 'config' / 'system.yaml'
        cmd_endpoint = 'ipc:///tmp/doly_eye_cmd.sock'
        try:
            import yaml
            cfg = yaml.safe_load(cfg_path.read_text())
            cmd_endpoint = cfg.get('zmq_service', {}).get('command_endpoint', cmd_endpoint)
        except Exception:
            pass

        ctx = zmq.Context()
        sock = ctx.socket(zmq.REQ)
        sock.connect(cmd_endpoint)
        cmd = {
            'action': 'play_sequence_animations',
            'sequence': args.sequence,
            'side': args.side,
            'delay_ms': args.delay_ms,
            'loop': args.loop,
            'fps': args.fps,
            'speed': args.speed
        }
        sock.send_json(cmd)
        resp = sock.recv_json()
        print(json.dumps(resp, indent=2, ensure_ascii=False))
        sock.close()
        ctx.term()

    elif args.command == "overlay_stop":
        cfg_path = Path(__file__).parent.parent.parent / 'config' / 'system.yaml'
        cmd_endpoint = 'ipc:///tmp/doly_eye_cmd.sock'
        try:
            import yaml
            cfg = yaml.safe_load(cfg_path.read_text())
            cmd_endpoint = cfg.get('zmq_service', {}).get('command_endpoint', cmd_endpoint)
        except Exception:
            pass

        ctx = zmq.Context()
        sock = ctx.socket(zmq.REQ)
        sock.connect(cmd_endpoint)
        cmd = {
            'action': 'stop_overlay_sequence',
            'overlay_id': args.overlay_id
        }
        sock.send_json(cmd)
        resp = sock.recv_json()
        print(json.dumps(resp, indent=2, ensure_ascii=False))
        sock.close()
        ctx.term()
    elif args.command == "eye_ping":
        print(json.dumps(send_eye_command({"action": "ping"}), indent=2, ensure_ascii=False))
    elif args.command == "eye_status":
        print(json.dumps(send_eye_command({"action": "get_status"}), indent=2, ensure_ascii=False))
    elif args.command == "eye_list":
        flags = {
            "categories": args.categories,
            "behaviors": args.behaviors,
            "iris": args.iris,
            "backgrounds": args.backgrounds
        }
        if not any(flags.values()) or args.all:
            flags = {k: True for k in flags}
        if flags.get("categories"):
            print("## categories")
            print(json.dumps(send_eye_command({"action": "list_categories"}), indent=2, ensure_ascii=False))
        if flags.get("behaviors"):
            print("## behaviors")
            print(json.dumps(send_eye_command({"action": "list_behaviors"}), indent=2, ensure_ascii=False))
        if flags.get("iris"):
            print("## iris")
            print(json.dumps(send_eye_command({"action": "list_iris"}), indent=2, ensure_ascii=False))
        if flags.get("backgrounds"):
            print("## backgrounds")
            print(json.dumps(send_eye_command({"action": "list_backgrounds"}), indent=2, ensure_ascii=False))
    elif args.command == "eye_set_brightness":
        cmd = {
            "action": "set_brightness",
            "level": args.level,
            "side": args.side
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_set_background":
        cmd = {
            "action": "set_background",
            "type": args.type,
            "style": args.style
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_set_iris":
        cmd = {
            "action": "set_iris",
            "theme": args.theme,
            "style": args.style,
            "side": args.side
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_play_animation":
        cmd = {
            "action": "play_animation",
            "animation": args.animation,
            "id": args.id,
            "fps": args.fps,
            "hold_duration": args.hold_duration
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_play_behavior":
        cmd = {
            "action": "play_behavior",
            "behavior": args.behavior,
            "level": args.level,
            "fps": args.fps,
            "hold_duration": args.hold_duration
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_play_category":
        cmd = {
            "action": "play_category",
            "category": args.category,
            "play_all": args.play_all,
            "fps": args.fps
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_play_seq":
        action = "play_sequence_animations"
        cmd = {
            "action": action,
            "sequence": None if args.sprite else args.sequence,
            "sprite": args.sequence if args.sprite else None,
            "side": args.side,
            "loop": args.loop,
            "fps": args.fps,
            "speed": args.speed,
            "delay_ms": args.delay_ms,
            "x": args.x,
            "y": args.y,
            "scale": args.scale,
            "rotation": args.rotation,
            "duration_ms": args.duration_ms
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_play_sprite":
        cmd = {
            "action": "play_sprite_animation",
            "category": args.category,
            "animation": args.animation,
            "start": args.start
        }
        if args.priority is not None:
            cmd["priority"] = args.priority
        print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_stop":
        print(json.dumps(send_eye_command({"action": "stop"}), indent=2, ensure_ascii=False))
    elif args.command == "eye_set_priority":
        if args.enable and args.disable:
            print("❌ 不能同时设置 enable 和 disable")
        else:
            cmd = {"action": "set_priority_enabled", "enabled": True if args.enable else False}
            if args.disable:
                cmd["enabled"] = False
            print(json.dumps(send_eye_command(cmd), indent=2, ensure_ascii=False))
    elif args.command == "eye_priority_status":
        print(json.dumps(send_eye_command({"action": "priority_status"}), indent=2, ensure_ascii=False))
    else:
        parser.print_help()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n👋 已退出")
    except Exception as e:
        logger.error(f"发生错误: {e}", exc_info=True)
