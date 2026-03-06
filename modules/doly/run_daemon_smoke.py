"""简短的 smoke 测试：初始化 DolyDaemon 并等待 TOF 初始化完成的异步任务日志

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import asyncio
import time
from modules.doly.daemon import DolyDaemon

async def main():
    d = DolyDaemon()
    ok = d.initialize()
    print("Daemon initialized:", ok)
    # wait a few seconds for async tasks to run
    await asyncio.sleep(3)
    # 检查是否有 tof_integrator
    has = hasattr(d, 'tof_integrator')
    print("Has tof_integrator:", has)
    if has:
        print("Tof initialized:", getattr(d, 'tof_integrator').initialized)
    # 停止可能的后台读取
    if hasattr(d, 'stop_tof_integration'):
        d.stop_tof_integration()

if __name__ == '__main__':
    asyncio.run(main())
