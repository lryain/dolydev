#pragma once

#include "EmotionState.hpp"
#include "Personality.hpp"
#include "EmotionDiscretizer.hpp"
#include "DynamicParameterGenerator.hpp"
#include "ShortTermMemory.hpp"
#include "LongTermMood.hpp"

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace eye_engine::emotion {

/**
 * @struct MultimodalCommand
 * @brief 多模态输出命令
 * 
 * 包含眼睛、肢体、RGB、TTS等所有模态的协调指令
 */
struct MultimodalCommand {
    EyeParameters eye_params;
    nlohmann::json servo_params;
    nlohmann::json rgb_params;
    nlohmann::json tts_params;
    
    uint64_t start_time_ms;
    std::map<std::string, int> timing_offsets;  // 各模态的相对延迟(ms)
};

/**
 * @class EmotionEngine
 * @brief Doly 情绪引擎主类
 * 
 * 职责:
 * 1. 维护连续的4维情绪状态
 * 2. 处理外部刺激事件
 * 3. 离散化情绪分类
 * 4. 生成眼睛和多模态参数
 * 5. 管理短期和长期记忆
 * 
 * 流程:
 *   外部刺激 → onStimulusEvent()
 *      ↓
 *   应用到 EmotionState
 *      ↓
 *   update() 衰减和回归
 *      ↓
 *   Discretizer 分类到8种情绪
 *      ↓
 *   ParameterGenerator 生成参数
 *      ↓
 *   返回 EyeParameters + MultimodalCommand
 */
class EmotionEngine {
public:
    EmotionEngine();
    ~EmotionEngine();
    
    /**
     * @brief 初始化情绪引擎
     * @param personality_preset 性格预设名称
     * @param config_dir 配置文件目录路径
     */
    void initialize(const std::string& personality_preset,
                   const std::string& config_dir);
    
    /**
     * @brief 每帧更新 (应该以 20-50Hz 调用)
     * @param dt 时间增量(秒)
     */
    void update(float dt);
    
    /**
     * @brief 处理外部刺激事件
     * @param stimulus_type touch/voice/success/error/timeout/wake_up
     * @param intensity 刺激强度 [0-1]
     * @param context 事件上下文(可选)
     */
    void onStimulusEvent(const std::string& stimulus_type,
                        float intensity,
                        const std::string& context = "");
    
    /**
     * @brief 直接设置离散情绪 (用于测试或直接驱动)
     * @param emotion_name happy/sad/curious/etc
     * @param intensity 情绪强度 [0-1]
     */
    void setDiscreteEmotion(const std::string& emotion_name, float intensity);
    
    /**
     * @brief 获取当前眼睛参数
     */
    const EyeParameters& getEyeParameters() const {
        return current_eye_params_;
    }
    
    /**
     * @brief 获取当前多模态命令
     */
    const MultimodalCommand& getMultimodalCommand() const {
        return current_multimodal_cmd_;
    }
    
    /**
     * @brief 获取当前连续情绪状态
     */
    const EmotionState& getEmotionState() const {
        return emotion_state_;
    }
    
    /**
     * @brief 获取当前离散情绪分类
     */
    const DiscreteEmotion& getDiscreteEmotion() const {
        return current_discrete_emotion_;
    }
    
    /**
     * @brief 获取性格模型
     */
    const Personality& getPersonality() const {
        return personality_;
    }
    
    /**
     * @brief 获取短期记忆(用于调试)
     */
    const ShortTermMemory& getMemory() const {
        return memory_;
    }
    
    /**
     * @brief 序列化为JSON (用于调试/日志)
     */
    nlohmann::json toJson() const;
    
private:
    // 核心组件
    EmotionState emotion_state_;
    Personality personality_;
    DiscreteEmotion current_discrete_emotion_;
    
    // 子模块
    std::unique_ptr<EmotionDiscretizer> discretizer_;
    std::unique_ptr<DynamicParameterGenerator> param_generator_;
    ShortTermMemory memory_;
    LongTermMood long_term_mood_;
    
    // 输出缓存
    EyeParameters current_eye_params_;
    MultimodalCommand current_multimodal_cmd_;
    
    // 配置参数
    std::map<std::string, float> stimulus_responses_;
    
    uint64_t last_update_time_ms_;
    
    /**
     * @brief 从配置文件加载刺激响应参数
     */
    void loadStimulusResponses(const std::string& config_path);
    
    /**
     * @brief 应用性格调节到刺激响应
     */
    float getAdjustedStimulusImpact(const std::string& stimulus_type) const;
};

} // namespace eye_engine::emotion
