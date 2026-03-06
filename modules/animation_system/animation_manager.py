"""
动画管理器

管理动画资源的加载、缓存和选择。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import random
import logging
from typing import Dict, List, Optional
from pathlib import Path

from .parser import AnimationParser, AnimationCategory, AnimationBlock

logger = logging.getLogger(__name__)


class AnimationManager:
    """动画管理器"""
    
    def __init__(self, animations_path: str):
        """
        初始化动画管理器
        
        Args:
            animations_path: 动画文件目录路径
        """
        # 兼容老路径和新路径
        if 'config/animations' in animations_path and not Path(animations_path).exists():
            animations_path = animations_path.replace('config/animations', 'assets/config/animations')
        self.animations_path = Path(animations_path)
        self.parser = AnimationParser(str(self.animations_path))
        self.categories: Dict[str, AnimationCategory] = {}
        self.cache: Dict[str, List[AnimationBlock]] = {}  # file_path -> blocks
        self.cache_enabled = True
    
    def load_animations(self, list_file: str = "animationlist.xml") -> None:
        """
        加载动画列表
        
        Args:
            list_file: 动画列表文件名
        """
        logger.info(f"Loading animations from {self.animations_path}")
        try:
            self.categories = self.parser.parse_animation_list(list_file)
            logger.info(f"Loaded {len(self.categories)} animation categories")
            
            # 可选：预加载常用动画到缓存
            if self.cache_enabled:
                self._preload_common_animations()
                
        except Exception as e:
            logger.error(f"Failed to load animations: {e}")
            raise
    
    def _preload_common_animations(self) -> None:
        """预加载常用动画到缓存"""
        common_categories = [
            'ANIMATION_HAPPY',
            'ANIMATION_WAKE_WORD',
            'ANIMATION_SPEAK',
        ]
        
        preload_count = 0
        for category_name in common_categories:
            category = self.categories.get(category_name)
            if not category:
                continue
            
            for level, file_paths in category.animations.items():
                for file_path in file_paths[:1]:  # 只预加载每个等级的第一个
                    try:
                        self._load_animation_file(file_path)
                        preload_count += 1
                    except Exception as e:
                        logger.warning(f"Failed to preload {file_path}: {e}")
        
        logger.info(f"Preloaded {preload_count} common animations")
    
    def _load_animation_file(self, file_path: str) -> List[AnimationBlock]:
        """
        加载动画文件（带缓存）
        
        Args:
            file_path: 动画文件路径
            
        Returns:
            动画块列表
        """
        # 检查缓存
        if self.cache_enabled and file_path in self.cache:
            logger.debug(f"Loading animation from cache: {file_path}")
            return self.cache[file_path]
        
        # 解析文件
        logger.debug(f"Parsing animation file: {file_path}")
        blocks = self.parser.parse_animation(file_path)
        
        # 存入缓存
        if self.cache_enabled:
            self.cache[file_path] = blocks
        
        return blocks
    
    def get_animation(
        self,
        category: str,
        level: int = 1,
        random_select: bool = True
    ) -> Optional[List[AnimationBlock]]:
        """
        获取动画
        
        Args:
            category: 动画分类名称
            level: 动画等级
            random_select: 是否随机选择（当有多个同等级动画时）
            
        Returns:
            动画块列表，如果没有找到则返回 None
        """
        category_obj = self.categories.get(category)
        if not category_obj:
            logger.warning(f"Category not found: {category}")
            return None
        
        # 获取该等级的所有动画文件
        file_paths = category_obj.get_animations(level)
        if not file_paths:
            logger.warning(f"No animations found for {category} level {level}")
            return None
        
        # 选择文件
        if random_select and len(file_paths) > 1:
            file_path = random.choice(file_paths)
        else:
            file_path = file_paths[0]
        
        logger.info(f"Selected animation file: {file_path} for category {category} level {level}")
        
        try:
            return self._load_animation_file(file_path)
        except Exception as e:
            logger.error(f"Failed to load animation {file_path}: {e}")
            return None
    
    def get_random_animation(self, category: str, level: int = 1) -> Optional[List[AnimationBlock]]:
        """
        随机获取动画
        
        Args:
            category: 动画分类名称
            level: 动画等级
            
        Returns:
            动画块列表
        """
        return self.get_animation(category, level, random_select=True)
    
    def get_animation_by_file(self, file_name: str) -> Optional[List[AnimationBlock]]:
        """
        通过文件名获取动画
        
        Args:
            file_name: 动画文件名
            
        Returns:
            动画块列表
        """
        file_path = str(self.animations_path / file_name)
        try:
            return self._load_animation_file(file_path)
        except Exception as e:
            logger.error(f"Failed to load animation {file_name}: {e}")
            return None
    
    def list_categories(self) -> List[str]:
        """获取所有分类名称"""
        return list(self.categories.keys())
    
    def get_category_info(self, category: str) -> Optional[Dict]:
        """
        获取分类信息
        
        Args:
            category: 分类名称
            
        Returns:
            分类信息字典
        """
        category_obj = self.categories.get(category)
        if not category_obj:
            return None
        
        return {
            'name': category_obj.name,
            'max_level': category_obj.max_level,
            'min_level': category_obj.min_level,
            'levels': list(category_obj.animations.keys()),
            'animation_counts': {
                level: len(files) 
                for level, files in category_obj.animations.items()
            }
        }
    
    def reload_animations(self) -> None:
        """重新加载所有动画"""
        logger.info("Reloading animations")
        self.cache.clear()
        self.load_animations()
    
    def clear_cache(self) -> None:
        """清空动画缓存"""
        logger.info(f"Clearing animation cache ({len(self.cache)} items)")
        self.cache.clear()
    
    def set_cache_enabled(self, enabled: bool) -> None:
        """
        设置是否启用缓存
        
        Args:
            enabled: 是否启用
        """
        self.cache_enabled = enabled
        if not enabled:
            self.clear_cache()
