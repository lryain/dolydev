"""
ZMQ 命令发布器

为 EventBus 的 ZMQ Socket 提供统一的命令发布接口

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import json
import logging
from typing import Any, Dict

logger = logging.getLogger(__name__)


class ZMQCommandPublisher:
    """ZMQ 命令发布器包装类"""
    
    def __init__(self, zmq_socket):
        """
        初始化
        
        Args:
            zmq_socket: ZMQ PUB socket 实例
        """
        self.socket = zmq_socket
    
    def publish_command(self, topic: str, data: Dict[str, Any]) -> bool:
        """
        发布 ZMQ 命令
        
        Args:
            topic: 命令主题 (例如: "cmd.vision.face.register")
            data: 命令数据字典
        
        Returns:
            bool: 是否发送成功
        """
        if not self.socket:
            logger.error(f"[ZMQCommandPublisher] Socket 未初始化")
            return False
        
        try:
            # 直接发送 topic + JSON payload
            # Vision Service 的 VisionBusBridge 会解析
            payload = json.dumps(data, ensure_ascii=False)
            self.socket.send_multipart([
                topic.encode('utf-8'),
                payload.encode('utf-8')
            ])
            logger.debug(f"[ZMQCommandPublisher] 📤 已发送: {topic}")
            return True
            
        except Exception as e:
            logger.error(f"[ZMQCommandPublisher] 发送失败: topic={topic}, error={e}")
            return False
