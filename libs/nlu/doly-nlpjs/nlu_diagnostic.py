#!/usr/bin/env python3
"""
NLU Service Diagnostic Tool - NLP.js 服务诊断工具

功能:
- 检查 NLP.js 服务状态
- 获取详细诊断信息
- 手动重启服务
- 显示实时监控信息

用法:
    python3 nlu_diagnostic.py status          # 查看服务状态
    python3 nlu_diagnostic.py diagnose        # 详细诊断报告
    python3 nlu_diagnostic.py restart         # 手动重启服务
    python3 nlu_diagnostic.py monitor         # 实时监控模式 (按 Ctrl+C 退出)
    python3 nlu_diagnostic.py health-check    # 快速健康检查

"""

import sys
import os
import time
import json
import argparse
from pathlib import Path

# 添加项目路径
sys.path.insert(0, str(Path(__file__).parent.parent))

from modules.nlu.nlu_manager import NLUManager


def print_header(title):
    """打印标题"""
    print("\n" + "=" * 70)
    print(f"  {title}")
    print("=" * 70)


def print_status(status_dict):
    """打印服务状态"""
    print("\n📊 服务状态:")
    print(f"  健康: {'✅ 是' if status_dict.get('is_healthy') else '❌ 否'}")
    print(f"  运行: {'✅ 是' if status_dict.get('is_running') else '❌ 否'}")
    print(f"  响应时间: {status_dict.get('response_time_ms', 0):.1f}ms")
    print(f"  运行时长: {status_dict.get('uptime_seconds', 0):.1f}s")
    print(f"  连续失败: {status_dict.get('consecutive_failures', 0)}")
    last_error = status_dict.get('last_error')
    if last_error:
        print(f"  最后错误: {last_error}")


def print_diagnostics(diag_dict):
    """打印诊断信息"""
    print("\n📈 诊断统计:")
    print(f"  总检查数: {diag_dict.get('total_checks', 0)}")
    print(f"  成功检查: {diag_dict.get('successful_checks', 0)}")
    print(f"  失败检查: {diag_dict.get('failed_checks', 0)}")
    print(f"  成功率: {diag_dict.get('success_rate', 0)*100:.1f}%")
    print(f"  自动重启: {diag_dict.get('auto_restarts', 0)} 次")
    print(f"  手动重启: {diag_dict.get('manual_restarts', 0)} 次")


def cmd_status(manager):
    """显示服务状态"""
    print_header("🔍 NLP.js 服务状态")
    
    status = manager.get_service_status()
    if status.get("status") == "monitor_disabled":
        print("⚠️  服务监控未启用")
        return
    
    if status.get("status") == "error":
        print(f"❌ 错误: {status.get('error')}")
        return
    
    print_status(status)


def cmd_diagnose(manager):
    """显示诊断报告"""
    print_header("🔧 NLP.js 服务诊断报告")
    
    report = manager.diagnose_service()
    print(report)
    
    diag = manager.get_service_diagnostics()
    if diag.get("status") != "monitor_disabled" and diag.get("status") != "error":
        print_diagnostics(diag)


def cmd_restart(manager):
    """手动重启服务"""
    print_header("🔄 手动重启 NLP.js 服务")
    
    print("⏳ 正在重启服务...")
    success = manager.restart_service()
    
    if success:
        print("✅ 服务重启成功")
        time.sleep(2)
        cmd_status(manager)
    else:
        print("❌ 服务重启失败")
        cmd_diagnose(manager)


def cmd_monitor(manager, interval=5):
    """实时监控模式"""
    print_header("📡 实时监控模式 (按 Ctrl+C 退出)")
    
    try:
        count = 0
        while True:
            count += 1
            print(f"\n[{count}] 检查时间: {time.strftime('%Y-%m-%d %H:%M:%S')}")
            
            status = manager.get_service_status()
            if status.get("status") == "monitor_disabled":
                print("⚠️  服务监控未启用")
                break
            
            if status.get("status") == "error":
                print(f"❌ 错误: {status.get('error')}")
            else:
                # 简洁输出
                healthy_str = "✅" if status.get('is_healthy') else "❌"
                running_str = "✅" if status.get('is_running') else "❌"
                response_ms = status.get('response_time_ms', 0)
                failures = status.get('consecutive_failures', 0)
                
                print(f"  健康: {healthy_str} | 运行: {running_str} | "
                      f"响应: {response_ms:.1f}ms | 失败: {failures}")
                
                last_error = status.get('last_error')
                if last_error:
                    print(f"  💬 {last_error}")
            
            time.sleep(interval)
            
    except KeyboardInterrupt:
        print("\n\n✅ 监控已停止")


def cmd_health_check(manager):
    """快速健康检查"""
    print_header("⚡ 快速健康检查")
    
    status = manager.get_service_status()
    
    if status.get("status") == "monitor_disabled":
        print("⚠️  服务监控未启用")
        return
    
    if status.get("status") == "error":
        print(f"❌ 检查失败: {status.get('error')}")
        return
    
    is_healthy = status.get('is_healthy', False)
    
    if is_healthy:
        print("✅ NLP.js 服务正常运行")
        print(f"   响应时间: {status.get('response_time_ms', 0):.1f}ms")
    else:
        print("❌ NLP.js 服务不健康")
        print(f"   连续失败: {status.get('consecutive_failures', 0)}")
        print(f"   错误: {status.get('last_error', '未知')}")
        print("\n💡 建议:")
        print("   1. 检查 /home/pi/dolydev/libs/nlu/doly-nlpjs 目录")
        print("   2. 运行: python3 nlu_diagnostic.py diagnose")
        print("   3. 尝试手动重启: python3 nlu_diagnostic.py restart")


def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description="NLU Service Diagnostic Tool - NLP.js 服务诊断工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s status          查看服务状态
  %(prog)s diagnose        详细诊断报告
  %(prog)s restart         手动重启服务
  %(prog)s monitor         实时监控模式
  %(prog)s health-check    快速健康检查
        """
    )
    
    parser.add_argument(
        'command',
        nargs='?',
        default='status',
        choices=['status', 'diagnose', 'restart', 'monitor', 'health-check'],
        help='要执行的命令'
    )
    
    parser.add_argument(
        '--interval',
        type=float,
        default=5.0,
        help='监控模式的检查间隔（秒）'
    )
    
    args = parser.parse_args()
    
    # 初始化管理器
    try:
        print("⏳ 初始化 NLUManager...")
        manager = NLUManager(config_path="config/nlu_config.yaml")
        if not manager.initialize():
            print("❌ NLUManager 初始化失败")
            sys.exit(1)
        print("✅ NLUManager 初始化成功")
    except Exception as e:
        print(f"❌ 初始化失败: {e}")
        sys.exit(1)
    
    try:
        # 执行命令
        if args.command == 'status':
            cmd_status(manager)
        elif args.command == 'diagnose':
            cmd_diagnose(manager)
        elif args.command == 'restart':
            cmd_restart(manager)
        elif args.command == 'monitor':
            cmd_monitor(manager, interval=args.interval)
        elif args.command == 'health-check':
            cmd_health_check(manager)
    finally:
        # 清理
        try:
            manager.shutdown()
        except:
            pass


if __name__ == '__main__':
    main()
