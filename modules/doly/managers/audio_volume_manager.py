"""
音量管理器 - 处理音量调整、静音、恢复等操作
直接使用 alsamixer 接口实现音量控制

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
import subprocess
from typing import Optional

logger = logging.getLogger(__name__)


class AudioVolumeManager:
    """音量管理器 - 使用 alsamixer 直接控制音频硬件"""
    
    # ALSA 硬件参数
    ALSA_CONTROL_NAME = 'Speaker'    # 音量控制名称（Doly 默认使用 Speaker）
    ALSA_MAX_VALUE = 255              # ALSA 最大值
    
    def __init__(self, config: Optional[dict] = None):
        """
        初始化音量管理器
        
        参数:
            config: 配置字典，包含：
                - max_volume: 最大原始音量值（默认127）
                - min_volume: 最小原始音量值（默认0）
                - step: 单次增减步长（默认10）
                - default_volume: 恢复音量原始值（默认80）
                - mute_volume: 静音时保留的最低原始值（默认30）
                - control_name: ALSA 控制器名称（默认 'Speaker'）
        """
        # 默认配置
        self.config = {
            'max_volume': 80,
            'min_volume': 0,
            'step': 30,
            'default_volume': 60,
            'mute_volume': 30,
            'control_name': self.ALSA_CONTROL_NAME,
        }
        
        # 更新用户配置
        if config:
            self.config.update(config)
        
        # 当前音量和静音状态（使用原始刻度值）
        self.current_volume = self._get_system_volume()
        self.last_nonzero_volume = self.current_volume if self.current_volume > 0 else self.config['default_volume']
        self.is_muted = False
        self.muted_volume = None  # 静音前的音量
        
        logger.info(
            f"🔊 音量管理器初始化完成: "
            f"当前音量={self.current_volume}, "
            f"步长={self.config['step']}, "
            f"控制器={self.config['control_name']}"
        )
    
    def _get_system_volume(self) -> int:
        """
        从系统获取当前音量（原始刻度值）
        
        返回:
            当前音量原始值
        """
        try:
            result = subprocess.run(
                ['amixer', 'sget', self.config['control_name']],
                capture_output=True,
                text=True,
                timeout=2
            )

            if result.returncode != 0:
                logger.warning(f"⚠️  无法获取音量状态: {result.stderr}")
                return self.config['default_volume']

            stdout = result.stdout or ''

            # 尝试解析 Limits（例如: "Limits: Playback 0 - 127")
            import re
            max_limit = None
            m = re.search(r'Limits:\s*Playback\s*\d+\s*-\s*(\d+)', stdout)
            if m:
                try:
                    max_limit = int(m.group(1))
                except Exception:
                    max_limit = None

            # 优先解析数值 Playback N（例如 Front Left: Playback 76 [60%])，直接返回原始值
            for line in stdout.split('\n'):
                if 'Playback' in line and 'Limits:' not in line and 'Playback channels' not in line:
                    # 提取第一个 Playback 后面的数字
                    m2 = re.search(r'Playback\s+(\d+)', line)
                    if m2:
                        try:
                            val = int(m2.group(1))
                            logger.debug(f"📊 系统音量 (raw): {val}/{max_limit or self.ALSA_MAX_VALUE}")
                            return max(0, val)
                        except Exception:
                            continue

            # 最后回退：尝试从任意 [xx%] 结合 limits 反推原始值
            bracket_any = re.search(r'\[(\d+)%\]', stdout)
            if bracket_any and max_limit:
                try:
                    return int(round((int(bracket_any.group(1)) / 100.0) * max_limit))
                except Exception:
                    pass

            return self.config['default_volume']
        except Exception as e:
            logger.error(f"❌ 获取系统音量失败: {e}")
            return self.config['default_volume']
    
    def _set_system_volume(self, volume_value: int) -> bool:
        """
        设置系统音量
        
        参数:
            volume_value: 目标音量原始值
            
        返回:
            是否设置成功
        """
        try:
            volume_value = max(self.config['min_volume'], min(volume_value, self.config['max_volume']))
            volume_arg = str(volume_value)
            
            # 使用 amixer 设置音量
            # 为确保 Doly 机器人在不同输出（音箱/耳机）下都能生效，默认同时尝试设置 Speaker 和 Headphone
            controls_to_set = [self.config['control_name']]
            if self.config['control_name'] == 'Speaker':
                controls_to_set.append('Headphone')
            
            success = True
            for ctrl in set(controls_to_set):
                result = subprocess.run(
                    ['amixer', 'sset', ctrl, volume_arg],
                    capture_output=True,
                    text=True,
                    timeout=2
                )
                if result.returncode != 0:
                    # 如果其中一个失败（比如硬件没有该控制项），仅记录警告
                    logger.warning(f"⚠️  设置 {ctrl} 音量失败: {result.stderr.strip()}")
                    if ctrl == self.config['control_name']:
                        success = False
            
            if success:
                if volume_value > 0:
                    self.last_nonzero_volume = volume_value
                logger.debug(f"📤 已设置音量: raw={volume_value} (arg: {volume_arg})")
                return True
            return False
        except Exception as e:
            logger.error(f"❌ 设置系统音量异常: {e}")
            return False
    
    def volume_up(self, step: Optional[int] = None) -> dict:
        """
        增加音量
        
        参数:
            step: 增加步长，如果为None则使用配置中的步长
            
        返回:
            包含操作结果的字典
        """
        if self.is_muted:
            logger.info("🔊 已取消静音")
            self.is_muted = False
        
        if step is None:
            step = self.config['step']
        
        old_volume = self.current_volume
        self.current_volume = min(
            self.current_volume + step,
            self.config['max_volume']
        )
        
        # 实际设置系统音量
        if self._set_system_volume(self.current_volume):
            logger.info(f"🔊 音量增加: {old_volume} → {self.current_volume}")
            return {
                'status': 'success',
                'message': f'音量已增加到 {self.current_volume}',
                'volume': self.current_volume,
                'previous_volume': old_volume,
            }
        else:
            self.current_volume = old_volume
            return {
                'status': 'error',
                'message': '设置音量失败',
                'volume': self.current_volume,
            }
    
    def volume_down(self, step: Optional[int] = None) -> dict:
        """
        减少音量
        
        参数:
            step: 减少步长，如果为None则使用配置中的步长
            
        返回:
            包含操作结果的字典
        """
        if self.is_muted:
            logger.info("🔊 已取消静音")
            self.is_muted = False
        
        if step is None:
            step = self.config['step']
        
        old_volume = self.current_volume
        self.current_volume = max(
            self.current_volume - step,
            self.config['min_volume']
        )
        
        # 实际设置系统音量
        if self._set_system_volume(self.current_volume):
            logger.info(f"🔊 音量减少: {old_volume} → {self.current_volume}")
            return {
                'status': 'success',
                'message': f'音量已减少到 {self.current_volume}',
                'volume': self.current_volume,
                'previous_volume': old_volume,
            }
        else:
            self.current_volume = old_volume
            return {
                'status': 'error',
                'message': '设置音量失败',
                'volume': self.current_volume,
            }
    
    def set_volume(self, volume: int) -> dict:
        """
        设置绝对音量
        
        参数:
            volume: 目标音量原始值
            
        返回:
            包含操作结果的字典
        """
        # 限制在有效范围内
        old_volume = self.current_volume
        self.current_volume = max(
            self.config['min_volume'],
            min(volume, self.config['max_volume'])
        )
        
        if self.is_muted:
            self.is_muted = False
        
        # 实际设置系统音量
        if self._set_system_volume(self.current_volume):
            logger.info(f"🔊 设置音量: {old_volume} → {self.current_volume}")
            return {
                'status': 'success',
                'message': f'音量已设置为 {self.current_volume}',
                'volume': self.current_volume,
                'previous_volume': old_volume,
            }
        else:
            self.current_volume = old_volume
            return {
                'status': 'error',
                'message': '设置音量失败',
                'volume': self.current_volume,
            }
    
    def mute(self) -> dict:
        """
        静音
        
        返回:
            包含操作结果的字典
        """
        if self.is_muted:
            logger.info("🔇 已处于静音状态")
            return {
                'status': 'success',
                'message': '已处于静音状态',
                'is_muted': True,
                'volume': 0,
            }
        
        snapshot_volume = self._get_system_volume()
        if snapshot_volume > 0:
            self.current_volume = snapshot_volume
            self.last_nonzero_volume = snapshot_volume

        mute_floor = int(self.config.get('mute_volume', 10))
        mute_floor = max(self.config['min_volume'], min(mute_floor, self.config['max_volume']))
        target_mute_volume = max(self.config['min_volume'], min(self.current_volume, mute_floor))

        if self.current_volume > 0:
            self.muted_volume = self.current_volume
        else:
            self.muted_volume = self.config['default_volume']

        self.is_muted = True
        
        # 实际设置系统音量为配置的低音量，而不是彻底归零
        if self._set_system_volume(target_mute_volume):
            logger.info(f"🔇 已静音，之前音量为 {self.muted_volume}，静音保底音量为 {target_mute_volume}")
            return {
                'status': 'success',
                'message': f'已静音（之前音量: {self.muted_volume}）',
                'is_muted': True,
                'volume': target_mute_volume,
                'previous_volume': self.muted_volume,
            }
        else:
            self.is_muted = False
            return {
                'status': 'error',
                'message': '静音失败',
                'is_muted': False,
            }
    
    def unmute(self) -> dict:
        """
        取消静音，恢复到默认音量
        
        返回:
            包含操作结果的字典
        """
        if not self.is_muted:
            logger.info("🔊 未处于静音状态")
            return {
                'status': 'success',
                'message': '未处于静音状态',
                'is_muted': False,
                'volume': self.current_volume,
            }
        
        restore_volume = int(self.config.get('default_volume', 60))
        restore_volume = max(self.config['min_volume'], min(restore_volume, self.config['max_volume']))

        self.is_muted = False
        self.current_volume = restore_volume
        
        # 实际设置系统音量
        if self._set_system_volume(self.current_volume):
            logger.info(f"🔊 已取消静音，恢复音量为 {self.current_volume}")
            self.muted_volume = None
            return {
                'status': 'success',
                'message': f'已取消静音，音量恢复为 {self.current_volume}',
                'is_muted': False,
                'volume': self.current_volume,
            }
        else:
            self.is_muted = True
            return {
                'status': 'error',
                'message': '取消静音失败',
                'is_muted': True,
            }
    
    def get_status(self) -> dict:
        """
        获取当前音量状态
        
        返回:
            包含状态信息的字典
        """
        # 每次都从系统读取最新的音量值
        system_volume = self._get_system_volume()
        self.current_volume = system_volume
        if system_volume > 0:
            self.last_nonzero_volume = system_volume
        
        return {
            'volume': self.current_volume,
            'is_muted': self.is_muted,
            'muted_volume': self.muted_volume,
            'max_volume': self.config['max_volume'],
            'min_volume': self.config['min_volume'],
            'step': self.config['step'],
            'default_volume': self.config['default_volume'],
            'mute_volume': self.config.get('mute_volume', 10),
            'control_name': self.config['control_name'],
        }
