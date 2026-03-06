"""
接口注册表

统一管理所有硬件和服务接口，提供接口查找和调用能力。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from typing import Dict, Any, Optional, Callable, List

logger = logging.getLogger(__name__)


class InterfaceRegistry:
    """
    接口注册表
    
    统一管理各类硬件和服务接口：
    - 眼睛引擎接口 (eye)
    - 驱动服务接口 (drive)
    - 音频播放接口 (audio)
    - 小组件服务接口 (widget)
    - 动画管理接口 (animation)
    
    使用示例:
        registry = InterfaceRegistry()
        registry.register('eye', eye_client)
        registry.register('drive', drive_client)
        
        eye = registry.get('eye')
        eye.set_expression('happy')
    """
    
    def __init__(self):
        """初始化注册表"""
        self._interfaces: Dict[str, Any] = {}
        self._interface_info: Dict[str, Dict[str, Any]] = {}
        self._hooks: Dict[str, List[Callable]] = {
            'on_register': [],
            'on_unregister': [],
            'on_call': []
        }
        logger.info("[InterfaceRegistry] 初始化完成")
    
    def register(
        self, 
        name: str, 
        interface: Any,
        description: str = "",
        version: str = "1.0.0"
    ) -> bool:
        """
        注册接口
        
        Args:
            name: 接口名称
            interface: 接口实例
            description: 接口描述
            version: 接口版本
            
        Returns:
            是否注册成功
        """
        if name in self._interfaces:
            logger.warning(f"[InterfaceRegistry] 接口已存在，将被覆盖: {name}")
        
        self._interfaces[name] = interface
        self._interface_info[name] = {
            'description': description,
            'version': version,
            'type': type(interface).__name__,
            'methods': self._get_public_methods(interface)
        }
        
        logger.info(f"[InterfaceRegistry] 注册接口: {name} ({type(interface).__name__})")
        
        # 触发钩子
        for hook in self._hooks['on_register']:
            try:
                hook(name, interface)
            except Exception as e:
                logger.error(f"[InterfaceRegistry] on_register 钩子执行失败: {e}")
        
        return True
    
    def unregister(self, name: str) -> bool:
        """
        注销接口
        
        Args:
            name: 接口名称
            
        Returns:
            是否注销成功
        """
        if name not in self._interfaces:
            logger.warning(f"[InterfaceRegistry] 接口不存在: {name}")
            return False
        
        interface = self._interfaces.pop(name)
        self._interface_info.pop(name, None)
        
        logger.info(f"[InterfaceRegistry] 注销接口: {name}")
        
        # 触发钩子
        for hook in self._hooks['on_unregister']:
            try:
                hook(name, interface)
            except Exception as e:
                logger.error(f"[InterfaceRegistry] on_unregister 钩子执行失败: {e}")
        
        return True
    
    def get(self, name: str) -> Optional[Any]:
        """
        获取接口
        
        Args:
            name: 接口名称
            
        Returns:
            接口实例，不存在则返回 None
        """
        interface = self._interfaces.get(name)
        if interface is None:
            logger.warning(f"[InterfaceRegistry] 接口不存在: {name}")
        return interface
    
    def has(self, name: str) -> bool:
        """检查接口是否存在"""
        return name in self._interfaces
    
    def list_interfaces(self) -> List[str]:
        """列出所有已注册接口名称"""
        return list(self._interfaces.keys())
    
    def get_interface_info(self, name: str) -> Optional[Dict[str, Any]]:
        """获取接口详细信息"""
        return self._interface_info.get(name)
    
    def get_all_info(self) -> Dict[str, Dict[str, Any]]:
        """获取所有接口信息"""
        return self._interface_info.copy()
    
    def call(
        self, 
        interface_name: str, 
        method_name: str, 
        *args, 
        **kwargs
    ) -> Any:
        """
        调用接口方法
        
        Args:
            interface_name: 接口名称
            method_name: 方法名称
            *args: 位置参数
            **kwargs: 关键字参数
            
        Returns:
            方法返回值
            
        Raises:
            ValueError: 接口或方法不存在
        """
        interface = self.get(interface_name)
        if interface is None:
            raise ValueError(f"接口不存在: {interface_name}")
        
        method = getattr(interface, method_name, None)
        if method is None or not callable(method):
            raise ValueError(f"方法不存在或不可调用: {interface_name}.{method_name}")
        
        # 触发 on_call 钩子
        for hook in self._hooks['on_call']:
            try:
                hook(interface_name, method_name, args, kwargs)
            except Exception as e:
                logger.error(f"[InterfaceRegistry] on_call 钩子执行失败: {e}")
        
        logger.debug(f"[InterfaceRegistry] 调用: {interface_name}.{method_name}")
        return method(*args, **kwargs)
    
    async def call_async(
        self, 
        interface_name: str, 
        method_name: str, 
        *args, 
        **kwargs
    ) -> Any:
        """
        异步调用接口方法
        
        与 call() 类似，但支持异步方法
        """
        interface = self.get(interface_name)
        if interface is None:
            raise ValueError(f"接口不存在: {interface_name}")
        
        method = getattr(interface, method_name, None)
        if method is None or not callable(method):
            raise ValueError(f"方法不存在或不可调用: {interface_name}.{method_name}")
        
        logger.debug(f"[InterfaceRegistry] 异步调用: {interface_name}.{method_name}")
        
        import asyncio
        if asyncio.iscoroutinefunction(method):
            return await method(*args, **kwargs)
        else:
            return method(*args, **kwargs)
    
    def add_hook(self, event: str, callback: Callable) -> None:
        """
        添加事件钩子
        
        Args:
            event: 事件名称 (on_register, on_unregister, on_call)
            callback: 回调函数
        """
        if event in self._hooks:
            self._hooks[event].append(callback)
    
    def remove_hook(self, event: str, callback: Callable) -> None:
        """移除事件钩子"""
        if event in self._hooks and callback in self._hooks[event]:
            self._hooks[event].remove(callback)
    
    def _get_public_methods(self, obj: Any) -> List[str]:
        """获取对象的公共方法列表"""
        methods = []
        for name in dir(obj):
            if not name.startswith('_'):
                attr = getattr(obj, name, None)
                if callable(attr):
                    methods.append(name)
        return methods
    
    def __contains__(self, name: str) -> bool:
        return name in self._interfaces
    
    def __getitem__(self, name: str) -> Any:
        interface = self.get(name)
        if interface is None:
            raise KeyError(f"接口不存在: {name}")
        return interface


# 全局单例
_global_registry: Optional[InterfaceRegistry] = None


def get_global_registry() -> InterfaceRegistry:
    """获取全局接口注册表单例"""
    global _global_registry
    if _global_registry is None:
        _global_registry = InterfaceRegistry()
    return _global_registry
