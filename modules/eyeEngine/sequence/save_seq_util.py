"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

import lz4.frame
from PIL import Image
import numpy as np

def save_seq(arr: np.ndarray, output_path: str, width=240, height=240):
    """
    将单帧 numpy RGBA 图像保存为 .seq 动画文件（仅一帧）
    """
    if arr.shape[0] != height or arr.shape[1] != width:
        from PIL import Image
        arr = np.array(Image.fromarray(arr).resize((width, height)))
    frames_data = arr.tobytes()
    compressed = lz4.frame.compress(frames_data)
    with open(output_path, 'wb') as f:
        f.write(compressed)
