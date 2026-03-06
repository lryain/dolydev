"""
TTS ZMQ 接口实现

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import json
import zmq
import zmq.asyncio
import logging
import uuid
from typing import Optional

from ..hardware_interfaces import TTSInterface

logger = logging.getLogger(__name__)

class ZMQTTSInterface(TTSInterface):
    """真实 TTS 接口，连接到 zmq_tts_service"""
    
    def __init__(self, endpoint: str = "ipc:///tmp/doly_tts_req.sock"):
        """
        初始化 TTS 接口
        
        Args:
            endpoint: TTS 服务的 ZMQ 请求地址
        """
        self.endpoint = endpoint
        self.ctx = zmq.asyncio.Context.instance()
        self.sock = None
        
    async def connect(self):
        """连接到 TTS 服务"""
        if self.sock is not None:
            return
            
        try:
            self.sock = self.ctx.socket(zmq.REQ)
            self.sock.connect(self.endpoint)
            # 设置超时，防止服务端未启动导致挂起
            self.sock.setsockopt(zmq.RCVTIMEO, 5000)
            self.sock.setsockopt(zmq.SNDTIMEO, 2000)
            self.sock.setsockopt(zmq.LINGER, 0)
            logger.info(f"已连接到 TTS 服务: {self.endpoint}")
        except Exception as e:
            logger.error(f"连接 TTS 服务失败: {e}")
            self.sock = None
            raise
        
    async def synthesize(self, request: dict) -> dict:
        """
        发送合成请求
        
        Args:
            request: TTS 请求数据
            
        Returns:
            TTS 服务响应
        """
        if self.sock is None:
            try:
                await self.connect()
            except Exception:
                return {"ok": False, "error": {"message": "TTS 服务连接失败"}}
            
        # 添加唯一请求 ID
        payload = dict(request)
        if 'req_id' not in payload:
            payload['req_id'] = str(uuid.uuid4())
            
        try:
            # 发送请求
            await self.sock.send_string(json.dumps(payload))
            
            # 接收响应
            reply = await self.sock.recv_string()
            response = json.loads(reply)
            
            if response.get('ok'):
                logger.info(f"[ZMQTTS] 合成成功: {response.get('path')}")
            else:
                error = response.get('error', {})
                logger.warning(f"[ZMQTTS] 合成失败: {error.get('message', '未知错误')}")
                
            return response
        except zmq.Again:
            logger.error("[ZMQTTS] 请求超时，服务可能未响应")
            # 超时后重置 socket 以便下次重连
            self.close()
            return {"ok": False, "error": {"message": "TTS 请求超时"}}
        except Exception as e:
            logger.error(f"[ZMQTTS] 通信错误: {e}")
            self.close()
            return {"ok": False, "error": {"message": f"TTS 通信错误: {e}"}}
        
    def close(self):
        """关闭连接"""
        if self.sock:
            try:
                self.sock.close(linger=0)
            except Exception:
                pass
            self.sock = None
