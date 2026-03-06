"""
硬件接口工厂

用于在真实硬件环境下创建驱动实现

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
import yaml
from pathlib import Path
from .hardware_interfaces import HardwareInterfaces
from .interfaces.zmq_eye_interface import ZMQEyeInterface
from .interfaces.led_interface_impl import ZmqLEDInterface
from .interfaces.audio_player_interface import AudioPlayerInterface
from .interfaces.drive_interface_impl import DriveSystemInterface
from .interfaces.zmq_tts_interface import ZMQTTSInterface

logger = logging.getLogger(__name__)

def create_real_hardware() -> HardwareInterfaces:
    """创建真实硬件接口集合"""
    logger.info("正在初始化真实硬件接口...")
    
    try:
        eye = ZMQEyeInterface()

        # 尝试从 EyeEngine 的默认配置中读取 task_priority.enabled 并应用到远端
        try:
            config_path = Path(__file__).resolve().parents[2] / "modules" / "eyeEngine" / "default_config.yaml"
            if config_path.exists():
                with open(config_path, "r") as fh:
                    cfg = yaml.safe_load(fh)
                tp = cfg.get("task_priority", {})
                if isinstance(tp, dict) and "enabled" in tp:
                    enabled = bool(tp.get("enabled", False))
                    # 将优先级设置发送到 EyeEngine（若失败，将在接口内部降级为本地标志）
                    try:
                        eye.set_priority_enabled(enabled)
                        logger.info(f"已根据 EyeEngine 配置设置 priority_enabled={enabled}")
                    except Exception as e:
                        logger.warning(f"设置 priority_enabled 时出错: {e}")
            else:
                logger.debug(f"未找到 EyeEngine 默认配置文件: {config_path}")
        except Exception as e:
            logger.warning(f"读取 EyeEngine 配置失败: {e}")

        led = ZmqLEDInterface()
        sound = AudioPlayerInterface()
        drive = DriveSystemInterface()
        tts = ZMQTTSInterface()
        
        # Arm 目前共用 DriveSystemInterface 的舵机控制
        # 这里我们将 drive 系统作为 arm 接口传入（如果它实现了 ArmInterface）
        # 实际上在 drive_interface_impl.py 中我们只实现了 DriveInterface
        # 我们需要在 DriveSystemInterface 中也继承 ArmInterface
        
        return HardwareInterfaces(
            eye=eye,
            led=led,
            sound=sound,
            drive=drive,
            arm=drive,  # 临时复用，后续可分离
            tts=tts
        )
    except Exception as e:
        logger.error(f"创建真实硬件失败: {e}")
        raise
