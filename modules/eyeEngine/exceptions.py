"""
自定义异常类

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""


class EyeEngineError(Exception):
    """眼睛引擎基础异常"""
    pass


class LcdDriverError(EyeEngineError):
    """LCD 驱动错误"""
    pass


class AssetNotFoundError(EyeEngineError):
    """资源未找到"""
    pass


class SeqDecodeError(EyeEngineError):
    """序列解码错误"""
    pass


class InvalidStateError(EyeEngineError):
    """无效状态错误"""
    pass


class NotInitializedError(EyeEngineError):
    """引擎未初始化"""
    pass
