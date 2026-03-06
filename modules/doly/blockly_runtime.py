"""
Blockly C++ Runtime Python 绑定

通过 ctypes 调用 C++ 库中的核心功能：
- DolyRuntime: 主控制循环
- BlocklyXMLParser: XML 解析
- 状态和事件管理

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import ctypes
import os
import logging
from pathlib import Path
from typing import Optional, Dict, Any

logger = logging.getLogger(__name__)


class BlocklyRuntimeError(Exception):
    """Blockly 运行时错误"""
    pass


class BlocklyRuntimeBinding:
    """
    Blockly C++ Runtime 的 Python 绑定
    
    使用 ctypes 调用 C++ 库的关键接口
    """
    
    def __init__(self, lib_path: Optional[str] = None):
        """
        初始化绑定
        
        Args:
            lib_path: C++ 库的路径（.so 文件）
                     如果为 None，则自动查找
        """
        self.lib_path = lib_path
        self.lib = None
        self._runtime_instance = None
        self._parser_instance = None
        
        if not self._load_library():
            raise BlocklyRuntimeError("无法加载 Blockly C++ 库")
        
        logger.info(f"✅ [BlocklyBinding] 库已加载: {self.lib_path}")
    
    def _find_library(self) -> Optional[str]:
        """
        自动查找库文件
        
        查找顺序（优先动态库，因为 ctypes 需要 .so 文件）：
        1. /home/pi/dolydev/libs/blockly/build/libblockly_core.so（开发环境）
        2. /usr/local/lib/libblockly_core.so（安装路径）
        3. /usr/lib/libblockly_core.so（系统路径）
        
        注：静态库 (.a) 无法通过 ctypes 加载，已改为生成共享库
        """
        search_paths = [
            # 开发环境（优先检查，因为 CMake 会创建 symlink）
            "/home/pi/dolydev/libs/blockly/build/libblockly_core.so",
            # 安装路径
            "/usr/local/lib/libblockly_core.so",
            "/usr/lib/libblockly_core.so",
        ]
        
        for path in search_paths:
            if os.path.exists(path):
                logger.info(f"📍 [BlocklyBinding] 找到库: {path}")
                return path
        
        logger.warning("[BlocklyBinding] 未找到预编译库")
        return None
    
    def _load_library(self) -> bool:
        """
        加载 C++ 库
        
        Returns:
            True 如果加载成功，否则 False
        """
        try:
            # 如果未指定路径，自动查找
            if not self.lib_path:
                self.lib_path = self._find_library()
            
            if not self.lib_path:
                logger.error("[BlocklyBinding] 无法定位库文件")
                return False
            
            # 加载动态库
            # 注：目前 C++ 库是静态库，需要通过 pybind11 或其他方式公开接口
            # 这里先创建 stub 实现，便于逐步集成
            try:
                self.lib = ctypes.CDLL(self.lib_path)
                logger.info("[BlocklyBinding] 使用 ctypes 加载库")
            except OSError as e:
                logger.warning(f"[BlocklyBinding] ctypes 加载失败: {e}")
                logger.info("[BlocklyBinding] 将使用 Python 实现作为备选")
                self.lib = None  # 标记使用 Python 实现
            
            return True
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] 加载库失败: {e}")
            return False
    
    def initialize(self) -> bool:
        """
        初始化运行时
        
        Returns:
            True 如果成功
        """
        try:
            if self.lib is None:
                logger.info("[BlocklyBinding] 使用 Python 实现的运行时")
                self._runtime_instance = PythonBlocklyRuntime()
                return True
            
            # 如果有 C++ 库，调用 C++ 接口
            # DolyRuntime_new() -> void*
            self.lib.DolyRuntime_new.restype = ctypes.c_void_p
            self._runtime_instance = self.lib.DolyRuntime_new()
            
            # DolyRuntime_initialize(void*)
            self.lib.DolyRuntime_initialize.argtypes = [ctypes.c_void_p]
            self.lib.DolyRuntime_initialize.restype = ctypes.c_bool
            
            if not self.lib.DolyRuntime_initialize(self._runtime_instance):
                raise BlocklyRuntimeError("C++ Runtime 初始化失败")
            
            logger.info("[BlocklyBinding] C++ Runtime 初始化成功")
            return True
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] 初始化失败: {e}")
            return False
    
    def parse_xml(self, xml_content: str) -> Optional[Dict[str, Any]]:
        """
        解析 Blockly XML
        
        Args:
            xml_content: XML 字符串内容
        
        Returns:
            解析结果字典，包含 program 结构
        """
        try:
            if self.lib is None:
                # 使用 Python 实现
                logger.info("[BlocklyBinding] 使用 Python XML 解析器")
                return parse_blockly_xml_python(xml_content)
            
            # 调用 C++ 解析器
            # BlocklyXMLParser_parse(const char* xml) -> 返回解析结果结构体
            # （需要在 C++ 侧定义返回结构体）
            logger.warning("[BlocklyBinding] C++ XML 解析器调用需要更多集成")
            
            # 临时方案：也使用 Python 实现
            return parse_blockly_xml_python(xml_content)
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] XML 解析失败: {e}")
            return None
    
    def start_program(self, program: Dict[str, Any]) -> bool:
        """
        启动 Blockly 程序
        
        Args:
            program: 解析后的程序结构
        
        Returns:
            True 如果启动成功
        """
        try:
            if self.lib is None:
                # 使用 Python 实现
                runtime = self._runtime_instance
                if isinstance(runtime, PythonBlocklyRuntime):
                    return runtime.start_program(program)
            else:
                # C++ 实现
                logger.warning("[BlocklyBinding] C++ 程序启动需要更多集成")
            
            return True
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] 启动程序失败: {e}")
            return False
    
    def stop_program(self) -> bool:
        """停止当前程序"""
        try:
            if self.lib is None:
                runtime = self._runtime_instance
                if isinstance(runtime, PythonBlocklyRuntime):
                    return runtime.stop_program()
            else:
                # C++ 实现
                self.lib.DolyRuntime_stop.argtypes = [ctypes.c_void_p]
                self.lib.DolyRuntime_stop(self._runtime_instance)
            
            return True
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] 停止程序失败: {e}")
            return False
    
    def push_event(self, event_type: str, data: Optional[Dict] = None) -> bool:
        """
        推送事件到运行时
        
        Args:
            event_type: 事件类型（如 'TOUCH_PRESSED'）
            data: 事件数据
        
        Returns:
            True 如果成功
        """
        try:
            if self.lib is None:
                runtime = self._runtime_instance
                if isinstance(runtime, PythonBlocklyRuntime):
                    return runtime.push_event(event_type, data or {})
            else:
                # C++ 实现
                logger.warning("[BlocklyBinding] C++ 事件推送需要更多集成")
            
            return True
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] 推送事件失败: {e}")
            return False
    
    def get_state(self) -> Optional[Dict[str, Any]]:
        """
        获取运行时状态
        
        Returns:
            状态字典
        """
        try:
            if self.lib is None:
                runtime = self._runtime_instance
                if isinstance(runtime, PythonBlocklyRuntime):
                    return runtime.get_state()
            else:
                # C++ 实现
                logger.warning("[BlocklyBinding] C++ 状态查询需要更多集成")
            
            return {}
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] 获取状态失败: {e}")
            return None
    
    def cleanup(self) -> None:
        """清理资源"""
        try:
            if self.lib is not None and self._runtime_instance:
                # 调用 C++ cleanup
                self.lib.DolyRuntime_delete.argtypes = [ctypes.c_void_p]
                self.lib.DolyRuntime_delete(self._runtime_instance)
            
            if self._runtime_instance and isinstance(self._runtime_instance, PythonBlocklyRuntime):
                self._runtime_instance.cleanup()
            
            logger.info("[BlocklyBinding] 清理完成")
            
        except Exception as e:
            logger.error(f"[BlocklyBinding] 清理失败: {e}")


class PythonBlocklyRuntime:
    """
    Python 实现的 Blockly Runtime（备选方案）
    
    当 C++ 库不可用时使用此实现
    """
    
    def __init__(self):
        self.current_program = None
        self.is_running = False
        self.state = {
            'touched': False,
            'cliff_detected': False,
            'obstacle_front': False,
            'is_moving': False,
            'pitch': 0.0,
            'roll': 0.0,
            'emotion': 'NEUTRAL'
        }
    
    def start_program(self, program: Dict[str, Any]) -> bool:
        """启动程序"""
        self.current_program = program
        self.is_running = True
        logger.info(f"[PythonRuntime] 程序已启动: {program.get('program_id', 'unknown')}")
        return True
    
    def stop_program(self) -> bool:
        """停止程序"""
        self.is_running = False
        logger.info("[PythonRuntime] 程序已停止")
        return True
    
    def push_event(self, event_type: str, data: Dict[str, Any]) -> bool:
        """推送事件"""
        logger.debug(f"[PythonRuntime] 事件: {event_type}, 数据: {data}")
        return True
    
    def get_state(self) -> Dict[str, Any]:
        """获取状态"""
        return self.state.copy()
    
    def cleanup(self) -> None:
        """清理"""
        self.is_running = False
        self.current_program = None


def parse_blockly_xml_python(xml_content: str) -> Optional[Dict[str, Any]]:
    """
    Python 实现的 Blockly XML 解析（备选方案）
    
    当 C++ 解析器不可用时使用此实现
    """
    try:
        import xml.etree.ElementTree as ET
        
        root = ET.fromstring(xml_content)
        
        # Blockly XML 可能有命名空间，需要处理
        # 尝试不同的方式查找 block
        first_block = root.find('block')
        
        # 如果没找到，尝试带命名空间的查找
        if first_block is None:
            # 获取命名空间
            namespaces = {'': 'http://www.w3.org/1999/xhtml'}
            # 尝试迭代所有子元素找 block
            for child in root:
                if child.tag.endswith('block') or 'block' in child.tag:
                    first_block = child
                    break
        
        if first_block is None:
            # 最后尝试：直接迭代 root 的所有后代
            for elem in root.iter():
                if elem.tag == 'block' or elem.tag.endswith('}block'):
                    first_block = elem
                    break
        
        if first_block is None:
            logger.error("[XMLParser] 未找到 block 元素")
            logger.debug(f"[XMLParser] XML root tag: {root.tag}")
            logger.debug(f"[XMLParser] XML children: {[child.tag for child in root]}")
            return None
        
        block_type = first_block.get('type')
        block_id = first_block.get('id', 'unknown')
        
        logger.info(f"[XMLParser] 解析块: {block_type} (ID: {block_id})")
        
        # 判断程序类型
        program_type = 'EVENT_DRIVEN' if block_type and block_type.startswith('doly_on_') else 'SEQUENCE'
        
        return {
            'program_id': block_id,
            'program_type': program_type,
            'block_type': block_type,
            'xml': xml_content,
            'timeline': {
                'duration_ms': 1000,  # 默认时长
                'actions': []
            }
        }
        
    except Exception as e:
        logger.error(f"[XMLParser] 解析失败: {e}")
        import traceback
        traceback.print_exc()
        return None
