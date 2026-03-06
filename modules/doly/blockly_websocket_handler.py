"""
WebSocket Blockly 处理器

处理从 Web 前端发送的 Blockly XML 程序，
并通过 Daemon 的 handle_blockly_program() 方法执行

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import json
import logging
from typing import Optional, Dict, Any

logger = logging.getLogger(__name__)


class BlocklyWebSocketHandler:
    """Blockly WebSocket 消息处理器"""
    
    def __init__(self, daemon):
        """
        初始化处理器
        
        Args:
            daemon: DolyDaemon 实例
        """
        self.daemon = daemon
        self.logger = logging.getLogger(__name__)
    
    async def handle_blockly_start(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """
        处理 Blockly 程序启动请求
        
        WebSocket 消息格式:
        {
            "action": "blockly_start",
            "data": {
                "xml": "<xml>...</xml>",
                "program_id": "optional_id",
                "auto_stop": false
            }
        }
        
        Args:
            data: 包含 xml、program_id 等的字典
            
        Returns:
            {
                "success": bool,
                "program_id": str,
                "error": optional error message
            }
        """
        try:
            xml_code = data.get('xml')
            if not xml_code:
                return {
                    "success": False,
                    "error": "Missing XML code in request"
                }
            
            # 调用 Daemon 的 Blockly 处理方法
            success = self.daemon.handle_blockly_program(xml_code)
            
            if success:
                self.logger.info("[Blockly WebSocket] 程序启动成功")
                return {
                    "success": True,
                    "message": "Blockly program started successfully"
                }
            else:
                self.logger.warning("[Blockly WebSocket] 程序启动失败")
                return {
                    "success": False,
                    "error": "Failed to start Blockly program"
                }
                
        except Exception as e:
            self.logger.error(f"[Blockly WebSocket] 异常: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    async def handle_blockly_stop(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """
        处理 Blockly 程序停止请求
        
        WebSocket 消息格式:
        {
            "action": "blockly_stop",
            "data": {
                "program_id": "optional_id"
            }
        }
        
        Args:
            data: 包含 program_id 等的字典
            
        Returns:
            {
                "success": bool,
                "error": optional error message
            }
        """
        try:
            if not self.daemon.blockly_runtime:
                return {
                    "success": False,
                    "error": "Blockly runtime not initialized"
                }
            
            # 停止程序
            self.daemon.blockly_runtime.stop_program()
            
            self.logger.info("[Blockly WebSocket] 程序已停止")
            return {
                "success": True,
                "message": "Blockly program stopped"
            }
            
        except Exception as e:
            self.logger.error(f"[Blockly WebSocket] 停止异常: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    async def handle_blockly_pause(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """
        处理 Blockly 程序暂停请求
        
        WebSocket 消息格式:
        {
            "action": "blockly_pause",
            "data": {}
        }
        
        Args:
            data: 消息数据
            
        Returns:
            {
                "success": bool,
                "error": optional error message
            }
        """
        try:
            if not self.daemon.blockly_runtime:
                return {
                    "success": False,
                    "error": "Blockly runtime not initialized"
                }
            
            # TODO: 实现暂停逻辑
            self.logger.info("[Blockly WebSocket] 程序已暂停（功能开发中）")
            return {
                "success": True,
                "message": "Blockly program paused"
            }
            
        except Exception as e:
            self.logger.error(f"[Blockly WebSocket] 暂停异常: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    async def handle_blockly_resume(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """
        处理 Blockly 程序恢复请求
        
        WebSocket 消息格式:
        {
            "action": "blockly_resume",
            "data": {}
        }
        
        Args:
            data: 消息数据
            
        Returns:
            {
                "success": bool,
                "error": optional error message
            }
        """
        try:
            if not self.daemon.blockly_runtime:
                return {
                    "success": False,
                    "error": "Blockly runtime not initialized"
                }
            
            # TODO: 实现恢复逻辑
            self.logger.info("[Blockly WebSocket] 程序已恢复（功能开发中）")
            return {
                "success": True,
                "message": "Blockly program resumed"
            }
            
        except Exception as e:
            self.logger.error(f"[Blockly WebSocket] 恢复异常: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    async def handle_blockly_status(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """
        获取 Blockly 程序执行状态
        
        WebSocket 消息格式:
        {
            "action": "blockly_status",
            "data": {}
        }
        
        Args:
            data: 消息数据
            
        Returns:
            {
                "success": bool,
                "status": {
                    "is_running": bool,
                    "current_program": str or None,
                    "mode": str,
                    "state": str
                },
                "error": optional error message
            }
        """
        try:
            status = {
                "is_running": False,
                "current_program": None,
                "mode": str(self.daemon.state_machine.current_mode),
                "state": str(self.daemon.state_machine.current_state)
            }
            
            if self.daemon.blockly_runtime:
                runtime_state = self.daemon.blockly_runtime.get_state()
                if runtime_state and isinstance(runtime_state, dict):
                    status["is_running"] = runtime_state.get('is_running', False)
                    status["current_program"] = runtime_state.get('current_program')
            
            return {
                "success": True,
                "status": status
            }
            
        except Exception as e:
            self.logger.error(f"[Blockly WebSocket] 状态查询异常: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    async def handle_blockly_validate(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """
        验证 Blockly XML 语法（不执行）
        
        WebSocket 消息格式:
        {
            "action": "blockly_validate",
            "data": {
                "xml": "<xml>...</xml>"
            }
        }
        
        Args:
            data: 包含 xml 的字典
            
        Returns:
            {
                "success": bool,
                "valid": bool,
                "program_type": str,
                "error": optional error message
            }
        """
        try:
            xml_code = data.get('xml')
            if not xml_code:
                return {
                    "success": False,
                    "valid": False,
                    "error": "Missing XML code"
                }
            
            if not self.daemon.blockly_runtime:
                return {
                    "success": False,
                    "valid": False,
                    "error": "Blockly runtime not initialized"
                }
            
            # 尝试解析 XML
            program = self.daemon.blockly_runtime.parse_xml(xml_code)
            
            if program:
                return {
                    "success": True,
                    "valid": True,
                    "program_type": program.get('program_type', 'UNKNOWN')
                }
            else:
                return {
                    "success": True,
                    "valid": False,
                    "error": "XML parsing failed"
                }
                
        except Exception as e:
            self.logger.error(f"[Blockly WebSocket] 验证异常: {e}")
            return {
                "success": False,
                "valid": False,
                "error": str(e)
            }
