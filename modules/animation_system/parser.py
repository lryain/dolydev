"""
XML 解析器

解析动画列表文件和单个动画文件。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import os
import xml.etree.ElementTree as ET
from typing import Dict, List, Optional, Any
from pathlib import Path
import logging

logger = logging.getLogger(__name__)


class AnimationCategory:
    """动画分类"""
    
    def __init__(self, name: str, max_level: int = 1, min_level: int = 1):
        """
        初始化动画分类
        
        Args:
            name: 分类名称
            max_level: 最大等级
            min_level: 最小等级
        """
        self.name = name
        self.max_level = max_level
        self.min_level = min_level
        self.animations: Dict[int, List[str]] = {}  # level -> [file_paths]
    
    def add_animation(self, level: int, file_path: str) -> None:
        """添加动画文件"""
        if level not in self.animations:
            self.animations[level] = []
        self.animations[level].append(file_path)
    
    def get_animations(self, level: int) -> List[str]:
        """获取指定等级的动画文件列表"""
        return self.animations.get(level, [])
    
    def __repr__(self) -> str:
        return f"AnimationCategory(name={self.name}, levels={list(self.animations.keys())})"


class AnimationBlock:
    """动画块"""
    
    def __init__(self, block_type: str):
        """
        初始化动画块
        
        Args:
            block_type: 块类型
        """
        self.block_type = block_type
        self.fields: Dict[str, Any] = {}
        self.statements: Dict[str, List['AnimationBlock']] = {}
    
    def add_field(self, name: str, value: Any) -> None:
        """添加字段"""
        self.fields[name] = value
    
    def add_statement(self, name: str, blocks: List['AnimationBlock']) -> None:
        """添加子语句"""
        self.statements[name] = blocks
    
    def get_field(self, name: str, default: Any = None) -> Any:
        """获取字段值"""
        return self.fields.get(name, default)
    
    def get_statement(self, name: str) -> List['AnimationBlock']:
        """获取子语句块列表"""
        return self.statements.get(name, [])
    
    def __repr__(self) -> str:
        return f"AnimationBlock(type={self.block_type}, fields={self.fields})"


class AnimationParser:
    """动画解析器"""
    
    def __init__(self, base_path: str):
        """
        初始化解析器
        
        Args:
            base_path: 动画文件基础路径
        """
        # 兼容老路径和新路径
        if 'config/animations' in base_path and not Path(base_path).exists():
            base_path = base_path.replace('config/animations', 'assets/config/animations')
        self.base_path = Path(base_path)
        self.categories: Dict[str, AnimationCategory] = {}
    
    def parse_animation_list(self, list_file: str = "animationlist.xml") -> Dict[str, AnimationCategory]:
        """
        解析动画列表文件
        
        Args:
            list_file: 列表文件名
            
        Returns:
            分类字典
        """
        list_path = self.base_path / list_file
        
        if not list_path.exists():
            raise FileNotFoundError(f"Animation list file not found: {list_path}")
        
        try:
            tree = ET.parse(list_path)
            root = tree.getroot()
            
            if root.tag != 'AnimationList':
                raise ValueError(f"Invalid root tag: {root.tag}, expected 'AnimationList'")
            
            self.categories.clear()
            
            for category_elem in root.findall('Category'):
                name = category_elem.get('name')
                if not name:
                    logger.warning("Found category without name, skipping")
                    continue
                
                max_level = int(category_elem.get('max', '1'))
                min_level = int(category_elem.get('min', '1'))
                
                category = AnimationCategory(name, max_level, min_level)
                
                for anim_block in category_elem.findall('AnimationBlocks'):
                    level = int(anim_block.get('level', '1'))
                    file_name = anim_block.get('file')
                    
                    if not file_name:
                        logger.warning(f"Animation block in {name} missing file attribute")
                        continue
                    
                    file_path = str(self.base_path / file_name)
                    category.add_animation(level, file_path)
                
                self.categories[name] = category
                # logger.info(f"Loaded category: {name} with {len(category.animations)} levels")
            
            # logger.info(f"Parsed {len(self.categories)} animation categories")
            return self.categories
            
        except ET.ParseError as e:
            raise ValueError(f"Failed to parse animation list XML: {e}")
        except Exception as e:
            raise RuntimeError(f"Error parsing animation list: {e}")
    
    def parse_animation(self, file_path: str) -> List[AnimationBlock]:
        """
        解析单个动画文件
        
        Args:
            file_path: 动画文件路径
            
        Returns:
            动画块列表
        """
        file_path = Path(file_path)
        
        if not file_path.exists():
            raise FileNotFoundError(f"Animation file not found: {file_path}")
        
        try:
            tree = ET.parse(file_path)
            root = tree.getroot()
            
            # 处理命名空间 - 移除命名空间前缀
            if '}' in root.tag:
                # 有命名空间，检查是否是 xml 标签
                tag_name = root.tag.split('}')[1]
                if tag_name != 'xml':
                    raise ValueError(f"Invalid root tag: {root.tag}, expected 'xml'")
            elif root.tag != 'xml':
                raise ValueError(f"Invalid root tag: {root.tag}, expected 'xml'")
            
            # 使用递归方式查找所有 block 元素（处理命名空间）
            blocks = []
            for block_elem in root.iter():
                # 获取不带命名空间的标签名
                tag_name = block_elem.tag.split('}')[1] if '}' in block_elem.tag else block_elem.tag
                
                # 只处理直接子元素的 block
                if tag_name == 'block' and self._is_direct_child(root, block_elem):
                    block = self._parse_block(block_elem)
                    if block:
                        blocks.append(block)
            
            logger.debug(f"Parsed {len(blocks)} blocks from {file_path.name}")
            return blocks
            
        except ET.ParseError as e:
            raise ValueError(f"Failed to parse animation XML {file_path}: {e}")
        except Exception as e:
            raise RuntimeError(f"Error parsing animation file {file_path}: {e}")
    
    def _parse_block(self, block_elem: ET.Element) -> Optional[AnimationBlock]:
        """
        解析单个动画块元素
        
        Args:
            block_elem: XML 块元素
            
        Returns:
            动画块对象
        """
        block_type = block_elem.get('type')
        if not block_type:
            logger.warning("Block element missing type attribute")
            return None
        
        block = AnimationBlock(block_type)
        
        # 解析字段 - 处理命名空间
        for field_elem in block_elem.iter():
            tag_name = field_elem.tag.split('}')[1] if '}' in field_elem.tag else field_elem.tag
            if tag_name == 'field' and self._is_child_of(block_elem, field_elem):
                field_name = field_elem.get('name')
                field_value = field_elem.text or ''
                
                if field_name:
                    # 尝试转换为合适的类型
                    block.add_field(field_name, self._convert_field_value(field_value))
        
        # 解析子语句 - 处理命名空间
        for statement_elem in block_elem.iter():
            tag_name = statement_elem.tag.split('}')[1] if '}' in statement_elem.tag else statement_elem.tag
            if tag_name == 'statement' and self._is_child_of(block_elem, statement_elem):
                statement_name = statement_elem.get('name')
                if not statement_name:
                    continue
                
                statement_blocks = []
                for sub_block_elem in statement_elem.iter():
                    sub_tag_name = sub_block_elem.tag.split('}')[1] if '}' in sub_block_elem.tag else sub_block_elem.tag
                    if sub_tag_name == 'block' and self._is_child_of(statement_elem, sub_block_elem):
                        sub_block = self._parse_block(sub_block_elem)
                        if sub_block:
                            statement_blocks.append(sub_block)
                
                block.add_statement(statement_name, statement_blocks)
                if block_type == 'repeat':
                    logger.info(f"[Parser] 解析 repeat 块的语句 '{statement_name}': 包含 {len(statement_blocks)} 个子块")
        
        # 解析 overlay 相关子元素（兼容 sequence_animations / sprites_animations / text_animations）
        overlay_items = []
        for child in block_elem:
            child_tag = child.tag.split('}')[1] if '}' in child.tag else child.tag
            if child_tag == 'sequence_animations':
                seq_name = child.get('name')
                loop_val = child.get('loop', 'false')
                delay_ms = child.get('delay_ms')
                fps_val = child.get('fps')
                side = child.get('side', 'BOTH')
                speed_val = child.get('speed')
                loop_count_val = child.get('loop_count')
                clear_time_val = child.get('clear_time')
                exclusive_val = child.get('exclusive', 'false')
                seq_spec = {
                    'type': 'sequence',
                    'name': seq_name,
                    'delay_ms': int(delay_ms) if delay_ms not in (None, '') else None,
                    'side': side,
                    'loop': True if str(loop_val).lower() == 'true' else False,
                    'loop_count': int(loop_count_val) if loop_count_val not in (None, '') else None,
                    'fps': int(fps_val) if fps_val is not None and fps_val != '' else None,
                    'speed': float(speed_val) if speed_val is not None and speed_val != '' else 1.0,
                    'clear_time': int(clear_time_val) if clear_time_val not in (None, '') else None,
                    'exclusive': True if str(exclusive_val).lower() == 'true' else False
                }
                overlay_items.append(seq_spec)
                # 保留旧字段，确保向后兼容
                block.add_field('sequence_animations', {
                    'name': seq_name,
                    'delay_ms': seq_spec['delay_ms'],
                    'side': side,
                    'loop': seq_spec['loop'],
                    'loop_count': seq_spec['loop_count'],
                    'clear_time': seq_spec['clear_time'],
                    'exclusive': seq_spec['exclusive'],
                    'fps': seq_spec['fps'],
                    'speed': seq_spec['speed']
                })
            elif child_tag == 'sprites_animations':
                loop_val = child.get('loop', 'false')
                loop_count_val = child.get('loop_count')
                speed_val = child.get('speed')
                start_val = child.get('start')
                clear_time_val = child.get('clear_time')
                overlay_items.append({
                    'type': 'sprite',
                    'category': child.get('category', ''),
                    'animation': child.get('name') or child.get('animation', ''),
                    'side': child.get('side', 'BOTH'),
                    'loop': True if str(loop_val).lower() == 'true' else False,
                    'loop_count': int(loop_count_val) if loop_count_val not in (None, '') else None,
                    'speed': float(speed_val) if speed_val not in (None, '') else 1.0,
                    'start': int(start_val) if start_val not in (None, '') else 0,
                    'clear_time': int(clear_time_val) if clear_time_val not in (None, '') else None
                })
            elif child_tag == 'text_animations':
                loop_count_val = child.get('loop_count')
                speed_val = child.get('speed')
                delay_ms_val = child.get('delay_ms')
                duration_ms_val = child.get('duration_ms')
                overlay_items.append({
                    'type': 'text',
                    'text': child.get('text', ''),
                    'font': child.get('font'),
                    'color': child.get('color'),
                    'bg_color': child.get('bg_color'),
                    'side': child.get('side', 'BOTH'),
                    'align': child.get('align', 'center'),
                    'scroll': child.get('scroll', 'none'),
                    'speed': float(speed_val) if speed_val not in (None, '') else 1.0,
                    'loop_count': int(loop_count_val) if loop_count_val not in (None, '') else None,
                    'delay_ms': int(delay_ms_val) if delay_ms_val not in (None, '') else None,
                    'duration_ms': int(duration_ms_val) if duration_ms_val not in (None, '') else None
                })

        if overlay_items:
            block.add_field('overlay_items', overlay_items)
        
        return block
    
    def _is_direct_child(self, parent: ET.Element, child: ET.Element) -> bool:
        """检查 child 是否是 parent 的直接子元素"""
        for elem in parent:
            if elem == child:
                return True
        return False
    
    def _is_child_of(self, parent: ET.Element, child: ET.Element) -> bool:
        """检查 child 是否是 parent 的子元素（包括间接）"""
        for elem in parent.iter():
            if elem == child and elem != parent:
                return True
        return False
    
    def _convert_field_value(self, value: str) -> Any:
        """
        转换字段值为合适的类型
        
        Args:
            value: 字符串值
            
        Returns:
            转换后的值
        """
        # 尝试转换为整数
        try:
            return int(value)
        except ValueError:
            pass
        
        # 尝试转换为浮点数
        try:
            return float(value)
        except ValueError:
            pass
        
        # 转换布尔值
        if value.upper() in ('TRUE', 'FALSE'):
            return value.upper() == 'TRUE'
        
        # 保持为字符串
        return value
    
    def get_category(self, name: str) -> Optional[AnimationCategory]:
        """
        获取动画分类
        
        Args:
            name: 分类名称
            
        Returns:
            动画分类对象
        """
        return self.categories.get(name)
    
    def list_categories(self) -> List[str]:
        """获取所有分类名称列表"""
        return list(self.categories.keys())
