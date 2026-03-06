"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
EyeEngine ZMQ 测试客户端

用于测试和演示 eyeEngine ZMQ 服务的功能
"""

import zmq
import json
import time
import argparse
import sys
from typing import Dict, Any, Optional

try:
    from eyeEngine.constants import MIN_FPS, MAX_FPS
except Exception:
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).parent.parent))
    from eyeEngine.constants import MIN_FPS, MAX_FPS


class EyeEngineClient:
    """EyeEngine ZMQ 客户端"""
    
    def __init__(self, cmd_endpoint: str = "ipc:///tmp/doly_eye_cmd.sock",
                 event_endpoint: str = "ipc:///tmp/doly_eye_event.sock"):
        """
        初始化客户端
        
        Args:
            cmd_endpoint: 命令端点
            event_endpoint: 事件端点
        """
        self.ctx = zmq.Context()
        
        # 命令套接字
        self.cmd_socket = self.ctx.socket(zmq.REQ)
        self.cmd_socket.connect(cmd_endpoint)
        self.cmd_socket.setsockopt(zmq.LINGER, 0)
        self.cmd_socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5 秒超时
        
        # 事件套接字
        self.event_socket = self.ctx.socket(zmq.SUB)
        self.event_socket.connect(event_endpoint)
        self.event_socket.setsockopt_string(zmq.SUBSCRIBE, "")  # 订阅所有事件
        self.event_socket.setsockopt(zmq.LINGER, 0)
        
        print(f"已连接到服务:")
        print(f"  命令端点: {cmd_endpoint}")
        print(f"  事件端点: {event_endpoint}")
    
    def send_command(self, cmd: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        """
        发送命令
        
        Args:
            cmd: 命令字典
        
        Returns:
            响应字典，失败返回 None
        """
        try:
            print(f"\n>>> 发送命令: {json.dumps(cmd, ensure_ascii=False)}")
            self.cmd_socket.send_json(cmd)
            
            response = self.cmd_socket.recv_json()
            print(f"<<< 收到响应: {json.dumps(response, ensure_ascii=False, indent=2)}")
            
            return response
        
        except zmq.error.Again:
            print("错误: 命令超时")
            return None
        except Exception as e:
            print(f"错误: {e}")
            return None
    
    def receive_events(self, timeout_ms: int = 1000) -> Optional[Dict[str, Any]]:
        """
        接收事件（非阻塞）
        
        Args:
            timeout_ms: 超时时间（毫秒）
        
        Returns:
            事件字典，无事件返回 None
        """
        try:
            if self.event_socket.poll(timeout_ms):
                event = self.event_socket.recv_json()
                # 展示 fps 信息（如果有）
                if isinstance(event, dict) and 'data' in event and isinstance(event['data'], dict):
                    if 'current_fps' in event['data']:
                        print(f"[事件详情] current_fps={event['data']['current_fps']}, last_average_fps={event['data'].get('last_average_fps')}")
                return event
        except Exception as e:
            print(f"接收事件错误: {e}")
        
        return None
    
    def close(self):
        """关闭客户端"""
        self.cmd_socket.close()
        self.event_socket.close()
        self.ctx.term()


def _format_event(event):
    """Return (type, data) tuple safely from an event object."""
    try:
        if isinstance(event, dict):
            etype = event.get('type', '<unknown>')
            if 'data' in event:
                return etype, event.get('data')
            # no explicit data key: return event without type
            other = {k: v for k, v in event.items() if k != 'type'}
            return etype, other if other else None
        # non-dict events
        return str(event), None
    except Exception as e:
        return '<error>', {'error': str(e), 'raw': event}


def _print_event(event):
    """Nicely print event content without assuming 'data' exists."""
    etype, data = _format_event(event)
    try:
        if data is None:
            print(f"[事件] {etype}: {json.dumps(event, ensure_ascii=False)}")
        else:
            try:
                print(f"[事件] {etype}: {json.dumps(data, ensure_ascii=False)}")
            except Exception:
                print(f"[事件] {etype}: {data}")
    except Exception as e:
        print(f"[事件] <print error>: {e}, raw={event}")


def wait_for_event(client: EyeEngineClient, event_type: str, timeout: int = 10, args=None):
    """等待特定事件"""
    print(f"\n监听 {event_type} 事件...")
    start = time.time()
    while time.time() - start < timeout:
        event = client.receive_events(timeout_ms=500)
        if event:
            _print_event(event)
            if event.get('type') == event_type:
                return event
        
        # 可选：显示当前 FPS
        if args and getattr(args, 'show_fps', False):
            status = client.send_command({"action": "get_status"})
            if status and status.get('success'):
                st = status['status']
                print(f"[状态] is_animating={st.get('is_animating')} current_fps={st.get('current_fps')} last_avg_fps={st.get('last_average_fps')}")
        time.sleep(0.1)
    return None

def test_ping(client: EyeEngineClient):
    """测试 ping"""
    print("\n=== 测试 ping ===")
    client.send_command({"action": "ping"})


def test_list_all(client: EyeEngineClient):
    """列出所有资源"""
    print("\n=== 列出所有资源 ===")
    
    print("\n--- 分类 ---")
    resp = client.send_command({"action": "list_categories"})
    if resp and resp.get("success"):
        print(f"分类: {', '.join(resp['categories'][:10])}...")
    
    print("\n--- 行为 ---")
    resp = client.send_command({"action": "list_behaviors"})
    if resp and resp.get("success"):
        print(f"行为: {', '.join(resp['behaviors'][:10])}...")
    
    print("\n--- 虹膜 ---")
    resp = client.send_command({"action": "list_iris"})
    if resp and resp.get("success"):
        for theme, styles in list(resp['iris'].items())[:3]:
            print(f"  {theme}: {', '.join(styles[:5])}...")
    
    print("\n--- 背景 ---")
    resp = client.send_command({"action": "list_backgrounds"})
    if resp and resp.get("success"):
        for bg_type, styles in resp['backgrounds'].items():
            print(f"  {bg_type}: {', '.join(styles[:5])}...")


def test_set_iris(client: EyeEngineClient):
    """测试设置虹膜"""
    print("\n=== 测试设置虹膜 ===")
    
    tests = [
        {"theme": "CLASSIC", "style": "COLOR_BLUE", "side": "BOTH"},
        {"theme": "CLASSIC", "style": "COLOR_GREEN", "side": "LEFT"},
        {"theme": "CLASSIC", "style": "COLOR_RED", "side": "RIGHT"},
    ]
    
    for test in tests:
        client.send_command({
            "action": "set_iris",
            **test,
            "priority": 7
        })
        time.sleep(1)


def test_set_background(client: EyeEngineClient):
    """测试设置背景"""
    print("\n=== 测试设置背景 ===")
    
    tests = [
        {"style": "COLOR_BLACK", "type": "COLOR"},
        {"style": "COLOR_WHITE", "type": "COLOR"},
    ]
    
    for test in tests:
        client.send_command({
            "action": "set_background",
            **test
        })
        time.sleep(1)


def test_brightness(client: EyeEngineClient):
    """测试亮度"""
    print("\n=== 测试亮度 ===")
    
    for level in [10, 5, 8]:
        client.send_command({
            "action": "set_brightness",
            "level": level
        })
        time.sleep(0.5)


def test_blink(client: EyeEngineClient):
    """测试眨眼"""
    print("\n=== 测试眨眼 ===")
    
    for i in range(3):
        client.send_command({"action": "blink", "priority": 6})
        time.sleep(1)


def test_play_behavior(client: EyeEngineClient, args=None):
    """测试播放行为"""
    print("\n=== 测试播放行为 ===")
    
    # 先列出行为
    resp = client.send_command({"action": "list_behaviors"})
    if not resp or not resp.get("success"):
        print("无法获取行为列表")
        return
    
    behaviors = resp.get("behaviors", [])
    if not behaviors:
        print("没有可用的行为")
        return
    
    # 播放第一个行为
    behavior = behaviors[0]
    print(f"\n播放行为: {behavior}")
    cmd = {
        "action": "play_behavior",
        "behavior": behavior,
        "priority": 7
    }
    if args and getattr(args, 'fps', None):
        cmd['fps'] = args.fps
    client.send_command(cmd)
    
    # 只有在指定了 --blocking 时才等待
    if args and getattr(args, 'blocking', False):
        # 等待并监听事件
        print("\n监听任务完成事件...")
        start = time.time()
        while time.time() - start < 10:  # 最多等待 10 秒
            event = client.receive_events(timeout_ms=500)
            if event:
                _print_event(event)
                if event.get('type') == 'task.complete':
                    break
            # 可选：显示当前 FPS
            if args and getattr(args, 'show_fps', False):
                status = client.send_command({"action": "get_status"})
                if status and status.get('success'):
                    st = status['status']
                    print(f"[状态] is_animating={st.get('is_animating')} current_fps={st.get('current_fps')} last_avg_fps={st.get('last_average_fps')}")
            time.sleep(0.2)


def test_priority(client: EyeEngineClient, args=None):
    """测试优先级打断"""
    print("\n=== 测试优先级打断 ===")
    
    # 先列出行为
    resp = client.send_command({"action": "list_behaviors"})
    if not resp or not resp.get("success"):
        print("无法获取行为列表")
        return
    
    behaviors = resp.get("behaviors", [])
    if len(behaviors) < 2:
        print("需要至少 2 个行为")
        return
    
    # 1. 启动低优先级任务
    print(f"\n1. 启动低优先级任务: {behaviors[0]} (优先级 3)")
    cmd_low = {
        "action": "play_behavior",
        "behavior": behaviors[0],
        "priority": 3
    }
    if args and getattr(args, 'fps', None):
        cmd_low['fps'] = args.fps
    client.send_command(cmd_low)
    
    time.sleep(1)
    
    # 2. 启动高优先级任务（应该打断）
    print(f"\n2. 启动高优先级任务: {behaviors[1]} (优先级 8)")
    cmd_high = {
        "action": "play_behavior",
        "behavior": behaviors[1],
        "priority": 8
    }
    if args and getattr(args, 'fps', None):
        cmd_high['fps'] = args.fps
    client.send_command(cmd_high)
    
    # 3. 监听打断事件
    print("\n3. 监听打断事件...")
    start = time.time()
    while time.time() - start < 10:
        event = client.receive_events(timeout_ms=500)
        if event:
            _print_event(event)
            if event.get('type') == 'task.complete':
                break
        # 可选：显示当前 FPS
        if args and getattr(args, 'show_fps', False):
            status = client.send_command({"action": "get_status"})
            if status and status.get('success'):
                st = status['status']
                print(f"[状态] is_animating={st.get('is_animating')} current_fps={st.get('current_fps')} last_avg_fps={st.get('last_average_fps')}")
        time.sleep(0.2)


def test_stop(client: EyeEngineClient):
    """测试停止任务"""
    print("\n=== 测试停止任务 ===")
    
    # 先列出行为
    resp = client.send_command({"action": "list_behaviors"})
    if not resp or not resp.get("success"):
        print("无法获取行为列表")
        return
    
    behaviors = resp.get("behaviors", [])
    if not behaviors:
        print("没有可用的行为")
        return
    
    # 启动任务
    print(f"\n启动任务: {behaviors[0]}")
    client.send_command({
        "action": "play_behavior",
        "behavior": behaviors[0],
        "priority": 5
    })
    
    time.sleep(1)
    
    # 停止任务
    print("\n停止任务")
    client.send_command({"action": "stop"})


def test_status(client: EyeEngineClient):
    """测试获取状态"""
    print("\n=== 测试获取状态 ===")
    client.send_command({"action": "get_status"})


def interactive_mode(client: EyeEngineClient):
    """交互模式"""
    print("\n=== 交互模式 ===")
    print("输入命令（JSON 格式），输入 'quit' 退出")
    print("示例: {\"action\": \"ping\"}")
    
    while True:
        try:
            cmd_str = input("\n> ")
            if cmd_str.strip().lower() == 'quit':
                break
            
            cmd = json.loads(cmd_str)
            client.send_command(cmd)
        
        except KeyboardInterrupt:
            break
        except json.JSONDecodeError as e:
            print(f"JSON 解析错误: {e}")
        except Exception as e:
            print(f"错误: {e}")


def main():
    parser = argparse.ArgumentParser(description='EyeEngine ZMQ 测试客户端')
    parser.add_argument('--cmd-endpoint', type=str, default='ipc:///tmp/doly_eye_cmd.sock',
                       help='命令端点')
    parser.add_argument('--event-endpoint', type=str, default='ipc:///tmp/doly_eye_event.sock',
                       help='事件端点')
    parser.add_argument('--test', type=str, choices=[
        'all', 'ping', 'list', 'iris', 'background', 'brightness', 
        'blink', 'behavior', 'priority', 'stop', 'status'
    ], help='运行预定义的特定测试套件')
    
    parser.add_argument('--interactive', '-i', action='store_true', help='交互模式')
    parser.add_argument('--status', action='store_true', help='获取当前状态')
    
    # 动作组：播放/列举选项 (互斥)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--list-anim', action='store_true', help='列出所有 XML 动画')
    group.add_argument('--list-seq', action='store_true', help='列出所有 .seq 序列文件')
    group.add_argument('--list-category', action='store_true', help='列出所有动画分类')
    group.add_argument('--list-behavior', action='store_true', help='列出所有行为')
    group.add_argument('--list-iris', action='store_true', help='列出所有虹膜类型和样式')
    group.add_argument('--list-background', action='store_true', help='列出所有背景')

    group.add_argument('--animation', type=str, help='按名称播放一个 XML 动画')
    group.add_argument('--anim-id', type=int, help='按 ID 播放一个 XML 动画')
    group.add_argument('--play-all', action='store_true', help='顺序播放所有 XML 动画')
    group.add_argument('--category', type=str, help='播放指定分类下的一个随机动画')
    group.add_argument('--behavior', type=str, help='播放一个行为 (来自 animationlist.xml)')
    group.add_argument('--sequence', type=str, help='播放指定名称的 .seq 序列')
    group.add_argument('--play-all-seq', action='store_true', help='顺序播放所有 .seq 序列文件')

    # 通用参数
    parser.add_argument('--play-category-all', action='store_true', help='配合 --category 使用，播放该分类下的所有动画')
    parser.add_argument('--level', type=int, default=1, help='行为等级 (默认 1)')
    parser.add_argument('--priority', type=int, default=5, help='指令优先级 (1-10, 默认 5)')
    parser.add_argument('--loop', action='store_true', help='循环播放 (仅适用于 --sequence 或 --play-all-seq)')
    parser.add_argument('--fps', type=int, default=None, help='覆盖播放帧率')
    parser.add_argument('--fps-seq', type=int, default=None, help='覆盖序列文件的播放帧率（优先于 --fps）')
    parser.add_argument('--blocking', action='store_true', help='等待动画播放完成')
    parser.add_argument('--show-fps', action='store_true', help='在测试期间显示实时 FPS')
    
    args = parser.parse_args()
    
    # 创建客户端
    client = EyeEngineClient(args.cmd_endpoint, args.event_endpoint)
    
    # 辅助功能：关闭客户端
    def close():
        client.close()
    
    try:
        if args.interactive:
            interactive_mode(client)
        elif args.status:
            test_status(client)
        
        # 处理列举命令
        elif args.list_anim:
            client.send_command({"action": "list_animations"})
        elif args.list_seq:
            client.send_command({"action": "list_sequences"})
        elif args.list_category:
            client.send_command({"action": "list_categories"})
        elif args.list_behavior:
            client.send_command({"action": "list_behaviors"})
        elif args.list_iris:
            client.send_command({"action": "list_iris"})
        elif args.list_background:
            client.send_command({"action": "list_backgrounds"})

        # 处理播放命令
        elif args.animation:
            cmd = {"action": "play_animation", "animation": args.animation, "priority": args.priority}
            if args.fps: cmd['fps'] = args.fps
            resp = client.send_command(cmd)
            if resp and resp.get('success') and args.blocking:
                wait_for_event(client, 'task.complete', timeout=60, args=args)
        
        elif args.anim_id is not None:
            cmd = {"action": "play_animation", "id": args.anim_id, "priority": args.priority}
            if args.fps: cmd['fps'] = args.fps
            resp = client.send_command(cmd)
            if resp and resp.get('success') and args.blocking:
                wait_for_event(client, 'task.complete', timeout=60, args=args)

        elif args.play_all:
            # 使用服务端批量播放命令
            cmd = {"action": "play_all_animations", "priority": args.priority}
            if args.fps: cmd['fps'] = args.fps
            resp = client.send_command(cmd)
            if resp and resp.get('success') and args.blocking:
                wait_for_event(client, 'task.complete', timeout=300, args=args)

        elif args.category:
            cmd = {
                "action": "play_category", 
                "category": args.category, 
                "play_all": args.play_category_all,
                "priority": args.priority
            }
            if args.fps: cmd['fps'] = args.fps
            resp = client.send_command(cmd)
            if resp and resp.get('success') and args.blocking:
                wait_for_event(client, 'task.complete', timeout=120, args=args)

        elif args.behavior:
            cmd = {
                "action": "play_behavior",
                "behavior": args.behavior,
                "level": args.level,
                "priority": args.priority
            }
            if args.fps: cmd['fps'] = args.fps
            resp = client.send_command(cmd)
            if resp and resp.get('success') and args.blocking:
                wait_for_event(client, 'task.complete', timeout=60, args=args)

        elif args.sequence:
            cmd = {
                "action": "play_sequence_animations",
                "sequence": args.sequence,
                "loop": args.loop,
                "priority": args.priority
            }
            # 优先使用 fps-seq
            actual_fps = args.fps_seq if args.fps_seq is not None else args.fps
            if actual_fps: cmd['fps'] = actual_fps
            resp = client.send_command(cmd)
            if resp and resp.get('success') and args.blocking:
                wait_for_event(client, 'task.complete', timeout=60, args=args)

        elif args.play_all_seq:
            cmd = {
                "action": "play_all_sequences",
                "loop": args.loop,
                "priority": args.priority
            }
            actual_fps = args.fps_seq if args.fps_seq is not None else args.fps
            if actual_fps: cmd['fps'] = actual_fps
            resp = client.send_command(cmd)
            if resp and resp.get('success') and args.blocking:
                wait_for_event(client, 'task.complete', timeout=300, args=args)

        elif args.test:
            if args.test == 'all':
                test_ping(client)
                test_list_all(client)
                test_set_iris(client)
                test_set_background(client)
                test_brightness(client)
                test_blink(client)
                test_play_behavior(client, args=args)
                test_priority(client, args=args)
                test_stop(client)
                test_status(client)
            elif args.test == 'ping':
                test_ping(client)
            elif args.test == 'list':
                test_list_all(client)
            elif args.test == 'iris':
                test_set_iris(client)
            elif args.test == 'background':
                test_set_background(client)
            elif args.test == 'brightness':
                test_brightness(client)
            elif args.test == 'blink':
                test_blink(client)
            elif args.test == 'behavior':
                test_play_behavior(client, args=args)
            elif args.test == 'priority':
                test_priority(client, args=args)
            elif args.test == 'stop':
                test_stop(client)
            elif args.test == 'status':
                test_status(client)
        else:
            # 默认运行简单测试
            test_ping(client)
            test_status(client)
            print("\n提示: 使用 --test <测试名> 运行特定测试，或使用详细参数(如 --animation, --sequence) 播放动画")
    
    except KeyboardInterrupt:
        print("\n\n用户中断")
    finally:
        close()


if __name__ == '__main__':
    main()
