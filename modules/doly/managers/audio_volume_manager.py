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
                - max_volume: 最大音量百分比（0-100，默认100）
                - min_volume: 最小音量百分比（0-100，默认0）
                - step: 单次增减步长百分比（默认10）
                - default_volume: 默认音量百分比（默认80）
                - control_name: ALSA 控制器名称（默认 'Headphone'）
        """
        # 默认配置
        self.config = {
            'max_volume': 100,
            'min_volume': 0,
            'step': 30,
            'default_volume': 80,
            'control_name': self.ALSA_CONTROL_NAME,
        }
        
        # 更新用户配置
        if config:
            self.config.update(config)
        
        # 当前音量和静音状态（使用百分比 0-100）
        self.current_volume = self._get_system_volume()
        self.is_muted = False
        self.muted_volume = None  # 静音前的音量
        
        logger.info(
            f"🔊 音量管理器初始化完成: "
            f"当前音量={self.current_volume}%, "
            f"步长={self.config['step']}%, "
            f"控制器={self.config['control_name']}"
        )
    
    def _get_system_volume(self) -> int:
        """
        从系统获取当前音量（百分比 0-100）
        
        返回:
            当前音量百分比
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
            
            # 解析输出，例如: "Front Left: Playback 100 [79%] [-21.00dB]"
            # 需要提取第一个 [xx%] 而不是最后一个 [xxx]
            for line in result.stdout.split('\n'):
                if 'Playback' in line and '[' in line:
                    # 查找第一个 '[' 和第一个 '%]'
                    first_bracket = line.find('[')
                    percent_end = line.find('%]', first_bracket)
                    
                    if first_bracket > 0 and percent_end > first_bracket:
                        try:
                            # 从 '[' 后面到 '%' 前面的内容
                            percentage = int(line[first_bracket + 1:percent_end])
                            logger.debug(f"📊 系统音量: {percentage}%")
                            return percentage
                        except ValueError:
                            pass
            
            return self.config['default_volume']
        except Exception as e:
            logger.error(f"❌ 获取系统音量失败: {e}")
            return self.config['default_volume']
    
    def _set_system_volume(self, volume_percent: int) -> bool:
        """
        设置系统音量
        
        参数:
            volume_percent: 目标音量百分比（0-100）
            
        返回:
            是否设置成功
        """
        try:
            # 转换百分比到 ALSA 值（0-127）
            alsa_value = int((volume_percent / 100.0) * self.ALSA_MAX_VALUE)
            alsa_value = max(0, min(alsa_value, self.ALSA_MAX_VALUE))
            
            # 使用 amixer 设置音量
            # 为确保 Doly 机器人在不同输出（音箱/耳机）下都能生效，默认同时尝试设置 Speaker 和 Headphone
            controls_to_set = [self.config['control_name']]
            if self.config['control_name'] == 'Speaker':
                controls_to_set.append('Headphone')
            
            success = True
            for ctrl in set(controls_to_set):
                result = subprocess.run(
                    ['amixer', 'sset', ctrl, str(alsa_value)],
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
                logger.debug(f"📤 已设置音量: {volume_percent}% (ALSA值: {alsa_value})")
                return True
            return False
        except Exception as e:
            logger.error(f"❌ 设置系统音量异常: {e}")
            return False
    
    def volume_up(self, step: Optional[int] = None) -> dict:
        """
        增加音量
        
        参数:
            step: 增加步长百分比，如果为None则使用配置中的步长
            
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
            logger.info(f"🔊 音量增加: {old_volume}% → {self.current_volume}%")
            return {
                'status': 'success',
                'message': f'音量已增加到 {self.current_volume}%',
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
            step: 减少步长百分比，如果为None则使用配置中的步长
            
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
            logger.info(f"🔊 音量减少: {old_volume}% → {self.current_volume}%")
            return {
                'status': 'success',
                'message': f'音量已减少到 {self.current_volume}%',
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
            volume: 目标音量百分比（0-100）
            
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
            logger.info(f"🔊 设置音量: {old_volume}% → {self.current_volume}%")
            return {
                'status': 'success',
                'message': f'音量已设置为 {self.current_volume}%',
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
        
        self.muted_volume = self.current_volume
        self.is_muted = True
        
        # 实际设置系统音量为 0
        if self._set_system_volume(0):
            logger.info(f"🔇 已静音，之前音量为 {self.muted_volume}%")
            return {
                'status': 'success',
                'message': f'已静音（之前音量: {self.muted_volume}%）',
                'is_muted': True,
                'volume': 0,
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
        取消静音，恢复到静音前的音量
        
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
        
        self.is_muted = False
        if self.muted_volume is not None:
            self.current_volume = self.muted_volume
        else:
            self.current_volume = self.config['default_volume']
        
        # 实际设置系统音量
        if self._set_system_volume(self.current_volume):
            logger.info(f"🔊 已取消静音，恢复音量为 {self.current_volume}%")
            return {
                'status': 'success',
                'message': f'已取消静音，音量恢复为 {self.current_volume}%',
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
        
        return {
            'volume': self.current_volume,
            'is_muted': self.is_muted,
            'muted_volume': self.muted_volume,
            'max_volume': self.config['max_volume'],
            'min_volume': self.config['min_volume'],
            'step': self.config['step'],
            'control_name': self.config['control_name'],
        }
