"""
常量定义

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

# LCD 尺寸
LCD_WIDTH = 240
LCD_HEIGHT = 240

# 虹膜尺寸 (根据实际图片)
IRIS_WIDTH = 153
IRIS_HEIGHT = 170

# 虹膜位置范围 (最大偏移像素)
IRIS_MAX_OFFSET_X = 30
IRIS_MAX_OFFSET_Y = 25

# 帧格式
FRAME_BPP = 4  # RGBA8888
FRAME_SIZE = LCD_WIDTH * LCD_HEIGHT * FRAME_BPP  # 230400 bytes

# RGB888 帧大小 (用于 LCD 输出)
RGB_FRAME_SIZE = LCD_WIDTH * LCD_HEIGHT * 3  # 172800 bytes

# LZ4 Frame Magic Number
LZ4F_MAGIC = 0x184D2204

# 可用的虹膜主题
IRIS_THEMES = [
    "classic",
    "digi",
    "food",
    "glow",
    "misc",
    "modern",
    "orbit",
    "seasonal",
    "space",
    "symbol",
]

# 可用的虹膜颜色 (classic 主题)
IRIS_COLORS = [
    "black", "blue", "brown", "cyan", "dark_blue", "dark_green",
    "dark_red", "gold", "gray", "green", "lime", "magenta",
    "orange", "pink", "purple", "red", "salmon", "sky_blue",
    "white", "yellow",
]

# 眼睑 ID 映射
# 侧眼睑 (L/R 后缀): 1-10, 15-18
LID_SIDE_IDS = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 17, 18]

# 垂直眼睑 (T/B 后缀): 11-14
LID_VERTICAL_IDS = [11, 12, 13, 14]

# 特殊眼睑
LID_SPECIAL = {
    "none": "None.png",
    "black": "black.png",
    "fireman": "fireman.png",
    "glass_left": "glassL.png",
    "glass_right": "glassR.png",
    "headband": "headband.png",
    "police": "police.png",
    "scan": "scan.png",
    "vr_left": "vrL.png",
    "vr_right": "vrR.png",
}

# 动画帧率范围
MIN_FPS = 1
MAX_FPS = 60

# 眨眼参数
BLINK_DURATION = 0.15  # 眨眼持续时间 (秒)
BLINK_FRAMES = 10       # 眨眼帧数
