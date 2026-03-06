"""
意图匹配器

负责将用户意图匹配到具体的任务定义，支持精确匹配和模糊匹配。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import re
import logging
from typing import Dict, Any, Optional, List, Tuple
from pathlib import Path
from difflib import SequenceMatcher

import yaml

from .models import Task, ActionType, IntentMapping

logger = logging.getLogger(__name__)


class IntentMatcher:
    """
    意图匹配器
    
    功能:
    - 从配置文件加载意图映射
    - 精确意图名称匹配
    - 关键词模糊匹配
    - 相似度匹配
    - 支持实体提取和验证
    
    使用示例:
        matcher = IntentMatcher('config/intent_action_mapping.yaml')
        task = matcher.match('set_timer', {'duration_min': 5})
        
        # 或者使用关键词匹配
        tasks = matcher.match_by_keywords('帮我定个5分钟的闹钟')
    """
    
    def __init__(self, config_path: Optional[str] = None):
        """
        初始化意图匹配器
        
        Args:
            config_path: 配置文件路径
        """
        self.config_path = Path(config_path) if config_path else None
        self.intent_mappings: Dict[str, IntentMapping] = {}
        self.keyword_patterns: Dict[str, List[str]] = {}  # intent -> keywords
        self.regex_patterns: Dict[str, re.Pattern] = {}   # intent -> regex
        
        # 默认配置
        self._default_threshold = 0.6  # 相似度阈值
        
        if self.config_path and self.config_path.exists():
            self._load_config()
        else:
            self._load_default_mappings()
        
        logger.info(f"[IntentMatcher] 初始化完成，加载 {len(self.intent_mappings)} 个意图映射")
    
    def _load_config(self) -> None:
        """从配置文件加载意图映射"""
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f) or {}
            
            intents = config.get('intents', {})
            for intent_name, intent_config in intents.items():
                mapping = IntentMapping(
                    intent=intent_name,
                    actions=intent_config.get('actions', []),
                    required_entities=intent_config.get('required_entities', []),
                    optional_entities=intent_config.get('optional_entities', []),
                    response_template=intent_config.get('response_template', ''),
                    priority=intent_config.get('priority', 5)
                )
                self.intent_mappings[intent_name] = mapping
                
                # 加载关键词
                keywords = intent_config.get('keywords', [])
                if keywords:
                    self.keyword_patterns[intent_name] = keywords
                
                # 加载正则表达式
                regex = intent_config.get('regex')
                if regex:
                    try:
                        self.regex_patterns[intent_name] = re.compile(regex)
                    except re.error as e:
                        logger.warning(f"[IntentMatcher] 无效的正则表达式 ({intent_name}): {e}")
            
            logger.info(f"[IntentMatcher] 从 {self.config_path} 加载配置完成")
            
        except Exception as e:
            logger.error(f"[IntentMatcher] 加载配置失败: {e}")
            self._load_default_mappings()
    
    def _load_default_mappings(self) -> None:
        """加载默认意图映射"""
        default_intents = {
            'greeting': IntentMapping(
                intent='greeting',
                actions=[
                    {'type': 'play_expression', 'params': {'category': 'EXPRESSION', 'name': 'happy'}}
                ],
                response_template='主人好~'
            ),
            'farewell': IntentMapping(
                intent='farewell',
                actions=[
                    {'type': 'play_animation', 'params': {'name': 'wave_hand'}}
                ],
                response_template='再见~'
            ),
            'set_timer': IntentMapping(
                intent='set_timer',
                actions=[
                    {'type': 'set_timer', 'params_from_entities': {'duration_min': 'duration_min', 'message': 'message'}}
                ],
                required_entities=['duration_min'],
                optional_entities=['message'],
                response_template='好的，{duration_min}分钟后提醒你'
            ),
            'query_time': IntentMapping(
                intent='query_time',
                actions=[
                    {'type': 'query_time', 'params': {}}
                ],
                response_template='现在是{current_time}'
            ),
            'play_music': IntentMapping(
                intent='play_music',
                actions=[
                    {'type': 'play_audio', 'params': {'type': 'music', 'name': 'random'}}
                ],
                response_template='好的~'
            ),
            'move_forward': IntentMapping(
                intent='move_forward',
                actions=[
                    {'type': 'move', 'params': {'direction': 'forward', 'duration_ms': 1000}}
                ],
                response_template='好的~'
            ),
            'move_backward': IntentMapping(
                intent='move_backward',
                actions=[
                    {'type': 'move', 'params': {'direction': 'backward', 'duration_ms': 1000}}
                ],
                response_template='好的~'
            ),
            'turn_left': IntentMapping(
                intent='turn_left',
                actions=[
                    {'type': 'move', 'params': {'direction': 'left', 'duration_ms': 500}}
                ],
                response_template='好的~'
            ),
            'turn_right': IntentMapping(
                intent='turn_right',
                actions=[
                    {'type': 'move', 'params': {'direction': 'right', 'duration_ms': 500}}
                ],
                response_template='好的~'
            ),
            'expression_happy': IntentMapping(
                intent='expression_happy',
                actions=[
                    {'type': 'play_expression', 'params': {'category': 'EXPRESSION', 'name': 'happy'}}
                ],
                response_template='嘿嘿~'
            ),
            'expression_sad': IntentMapping(
                intent='expression_sad',
                actions=[
                    {'type': 'play_expression', 'params': {'category': 'EXPRESSION', 'name': 'sad'}}
                ],
                response_template='呜...'
            )
        }
        
        self.intent_mappings = default_intents
        
        # 默认关键词映射
        self.keyword_patterns = {
            'greeting': ['你好', '早上好', '晚上好', '嗨', 'hello', 'hi'],
            'farewell': ['再见', '拜拜', 'bye', '晚安'],
            'set_timer': ['定时', '闹钟', '提醒', '分钟后'],
            'query_time': ['几点', '时间', '现在'],
            'play_music': ['放音乐', '播放音乐', '来点音乐'],
            'move_forward': ['向前', '前进', '往前走'],
            'move_backward': ['向后', '后退', '往后走'],
            'turn_left': ['向左', '左转', '转左'],
            'turn_right': ['向右', '右转', '转右'],
            'expression_happy': ['笑一个', '开心', '高兴'],
            'expression_sad': ['难过', '悲伤', '哭']
        }
        
        logger.info("[IntentMatcher] 使用默认意图映射")
    
    def match(
        self, 
        intent: str, 
        entities: Optional[Dict[str, Any]] = None
    ) -> Optional[List[Task]]:
        """
        精确匹配意图
        
        Args:
            intent: 意图名称
            entities: 实体字典
            
        Returns:
            任务列表，匹配失败返回 None
        """
        entities = entities or {}
        
        mapping = self.intent_mappings.get(intent)
        if not mapping:
            logger.warning(f"[IntentMatcher] 未找到意图映射: {intent}")
            return None
        
        # 验证必需实体
        if not mapping.validate_entities(entities):
            logger.warning(f"[IntentMatcher] 缺少必需实体: {intent}, 需要: {mapping.required_entities}")
            return None
        
        # 创建任务列表
        tasks = []
        for action_config in mapping.actions:
            action_type = action_config.get('type', 'custom')
            
            # 处理参数
            params = action_config.get('params', {}).copy()
            
            # 从实体填充参数
            params_from_entities = action_config.get('params_from_entities', {})
            for param_name, entity_name in params_from_entities.items():
                if entity_name in entities:
                    params[param_name] = entities[entity_name]
            
            task = Task(
                action_type=action_type,
                params=params,
                priority=mapping.priority,
                source='intent_matcher',
                metadata={'intent': intent, 'entities': entities}
            )
            tasks.append(task)
        
        logger.debug(f"[IntentMatcher] 匹配意图 {intent} -> {len(tasks)} 个任务")
        return tasks
    
    def match_by_keywords(
        self, 
        text: str,
        extract_entities: bool = True
    ) -> Optional[Tuple[str, Dict[str, Any], List[Task]]]:
        """
        通过关键词匹配意图
        
        Args:
            text: 用户输入文本
            extract_entities: 是否提取实体
            
        Returns:
            (匹配的意图, 提取的实体, 任务列表) 或 None
        """
        text_lower = text.lower()
        
        # 先尝试正则匹配
        for intent, pattern in self.regex_patterns.items():
            match = pattern.search(text)
            if match:
                entities = match.groupdict() if extract_entities else {}
                tasks = self.match(intent, entities)
                if tasks:
                    logger.info(f"[IntentMatcher] 正则匹配成功: {intent}")
                    return (intent, entities, tasks)
        
        # 关键词匹配
        best_match = None
        best_score = 0
        
        for intent, keywords in self.keyword_patterns.items():
            for keyword in keywords:
                # 确保 keyword 是字符串
                keyword_str = str(keyword)
                if keyword_str.lower() in text_lower:
                    # 简单评分：匹配的关键词长度 / 文本长度
                    score = len(keyword_str) / len(text) if text else 0
                    if score > best_score:
                        best_score = score
                        best_match = intent
        
        if best_match:
            entities = self._extract_entities(text, best_match) if extract_entities else {}
            tasks = self.match(best_match, entities)
            if tasks:
                logger.info(f"[IntentMatcher] 关键词匹配成功: {best_match} (score={best_score:.2f})")
                return (best_match, entities, tasks)
        
        logger.debug(f"[IntentMatcher] 未匹配到意图: {text[:50]}...")
        return None
    
    def match_by_similarity(
        self, 
        intent: str,
        threshold: Optional[float] = None
    ) -> Optional[str]:
        """
        通过相似度匹配意图
        
        Args:
            intent: 输入的意图名称（可能不完全匹配）
            threshold: 相似度阈值
            
        Returns:
            最匹配的意图名称或 None
        """
        threshold = threshold or self._default_threshold
        
        best_match = None
        best_ratio = 0
        
        for known_intent in self.intent_mappings.keys():
            ratio = SequenceMatcher(None, intent.lower(), known_intent.lower()).ratio()
            if ratio > best_ratio and ratio >= threshold:
                best_ratio = ratio
                best_match = known_intent
        
        if best_match:
            logger.debug(f"[IntentMatcher] 相似度匹配: {intent} -> {best_match} (ratio={best_ratio:.2f})")
        
        return best_match
    
    def _extract_entities(self, text: str, intent: str) -> Dict[str, Any]:
        """
        从文本中提取实体
        
        这是一个简单的实现，后续可以集成更复杂的 NLU
        """
        entities = {}
        
        # 提取数字（用于 duration_min 等）
        numbers = re.findall(r'(\d+)', text)
        if numbers and intent == 'set_timer':
            entities['duration_min'] = int(numbers[0])
        
        # 提取常见实体模式
        # TODO: 集成 doly-nlpjs 进行更精确的实体提取
        
        return entities
    
    def register_intent(
        self,
        intent: str,
        mapping: IntentMapping,
        keywords: Optional[List[str]] = None,
        regex: Optional[str] = None
    ) -> None:
        """
        注册新的意图映射
        
        Args:
            intent: 意图名称
            mapping: 意图映射配置
            keywords: 关键词列表
            regex: 正则表达式
        """
        self.intent_mappings[intent] = mapping
        
        if keywords:
            self.keyword_patterns[intent] = keywords
        
        if regex:
            try:
                self.regex_patterns[intent] = re.compile(regex)
            except re.error as e:
                logger.warning(f"[IntentMatcher] 无效的正则表达式 ({intent}): {e}")
        
        logger.info(f"[IntentMatcher] 注册意图: {intent}")
    
    def unregister_intent(self, intent: str) -> bool:
        """注销意图映射"""
        if intent not in self.intent_mappings:
            return False
        
        del self.intent_mappings[intent]
        self.keyword_patterns.pop(intent, None)
        self.regex_patterns.pop(intent, None)
        
        logger.info(f"[IntentMatcher] 注销意图: {intent}")
        return True
    
    def list_intents(self) -> List[str]:
        """列出所有已注册意图"""
        return list(self.intent_mappings.keys())
    
    def get_intent_info(self, intent: str) -> Optional[Dict[str, Any]]:
        """获取意图详细信息"""
        mapping = self.intent_mappings.get(intent)
        if not mapping:
            return None
        
        return {
            'intent': mapping.intent,
            'actions': mapping.actions,
            'required_entities': mapping.required_entities,
            'optional_entities': mapping.optional_entities,
            'response_template': mapping.response_template,
            'keywords': self.keyword_patterns.get(intent, []),
            'has_regex': intent in self.regex_patterns
        }
    
    def reload_config(self) -> bool:
        """重新加载配置"""
        if self.config_path and self.config_path.exists():
            self._load_config()
            return True
        return False
