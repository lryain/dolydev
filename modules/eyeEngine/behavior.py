"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

import os
import random
import logging
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)

class BehaviorManager:
    """
    管理 animationlist.xml 及其引用的逻辑动画列表。
    """
    def __init__(self, animation_dir: str):
        self.animation_dir = Path(animation_dir)
        self.animation_list_path = self.animation_dir / "animationlist.xml"
        self.categories: Dict[str, Dict[int, List[str]]] = {}  # category_name -> {level -> [files]}
        self._load_animation_list()

    def _load_animation_list(self):
        """加载 animationlist.xml"""
        if not self.animation_list_path.exists():
            logger.error(f"Animation list file not found: {self.animation_list_path}")
            return

        try:
            tree = ET.parse(self.animation_list_path)
            root = tree.getroot()
            for category in root.findall("Category"):
                cat_name = category.get("name")
                if not cat_name:
                    continue
                
                levels = {}
                for block in category.findall("AnimationBlocks"):
                    level = int(block.get("level", 1))
                    filename = block.get("file")
                    if filename:
                        if level not in levels:
                            levels[level] = []
                        levels[level].append(filename)
                
                self.categories[cat_name] = levels
            logger.info(f"Loaded {len(self.categories)} behavior categories from {self.animation_list_path}")
        except Exception as e:
            logger.error(f"Failed to load animation list: {e}")

    def get_available_behaviors(self) -> List[str]:
        """返回所有可用的行为类别名称"""
        return list(self.categories.keys())

    def get_random_animation_for_behavior(self, behavior_name: str, level: int = 1) -> Optional[Tuple[str, str]]:
        """
        从指定行为和等级中随机选择一个眼睛动画。
        返回 (category, animation_name) 元组。
        """
        if behavior_name not in self.categories:
            logger.warning(f"Behavior {behavior_name} not found")
            return None
        
        levels = self.categories[behavior_name]
        # 如果请求的 level 不存在，尝试找最近的 level 或默认 level 1
        if level not in levels:
            if not levels:
                return None
            level = list(levels.keys())[0]
            
        files = levels[level]
        if not files:
            return None
        
        chosen_file = random.choice(files)
        file_path = self.animation_dir / chosen_file
        
        return self._parse_eye_animation_from_block(file_path)

    def get_random_animation_file(self, behavior_name: str, level: int = 1) -> Optional[Tuple[str, str, Path]]:
        """
        从指定行为和等级中随机选择一个眼睛动画，并返回 (category, animation_name, file_path)
        """
        if behavior_name not in self.categories:
            logger.warning(f"Behavior {behavior_name} not found")
            return None

        levels = self.categories[behavior_name]
        if level not in levels:
            if not levels:
                return None
            level = list(levels.keys())[0]

        files = levels[level]
        if not files:
            return None

        chosen_file = random.choice(files)
        file_path = self.animation_dir / chosen_file
        result = self._parse_eye_animation_from_block(file_path)
        if result:
            category, animation = result
            return (category, animation, file_path)
        return None

    def _parse_eye_animation_from_block(self, file_path: Path) -> Optional[Tuple[str, str]]:
        """从 Blockly XML 文件中提取眼睛动画分类和名称"""
        if not file_path.exists():
            logger.error(f"Block file not found: {file_path}")
            return None
        
        try:
            tree = ET.parse(file_path)
            root = tree.getroot()
            
            # 处理命名空间
            ns = {'ns': 'https://developers.google.com/blockly/xml'}
            
            # 查找 type="eye_animations" 的 block
            # 支持有命名空间和无命名空间的情况
            blocks = root.findall(".//ns:block[@type='eye_animations']", ns)
            if not blocks:
                blocks = root.findall(".//block[@type='eye_animations']")
                
            for block in blocks:
                category_field = block.find("ns:field[@name='category']", ns)
                if category_field is None:
                    category_field = block.find("field[@name='category']")
                    
                animation_field = block.find("ns:field[@name='animation']", ns)
                if animation_field is None:
                    animation_field = block.find("field[@name='animation']")
                    
                if category_field is not None and animation_field is not None:
                    return (category_field.text, animation_field.text)
        except Exception as e:
            logger.error(f"Error parsing block file {file_path}: {e}")
        
        return None
