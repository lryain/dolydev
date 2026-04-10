#pragma once

#include <string>
#include <map>
#include <nlohmann/json.hpp>

namespace eye_engine::emotion {

struct EmotionState;
struct Personality;
struct DiscreteEmotion;

/**
 * @struct EyeParameters
 * @brief 眼睛表情的完整参数集
 */
struct EyeParameters {
    // 瞳孔
    float pupil_size_multiplier;      // 1.0 = 基础大小
    float pupil_transition_speed;
    float pupil_light_sensitivity;
    
    // 眼球运动
    std::string gaze_pattern;         // micro_movement/circle/scan/etc
    float gaze_speed_multiplier;
    float gaze_amplitude_multiplier;
    float gaze_latency_ms;            // 反应延迟 50-200ms
    float gaze_overshoot;             // 过冲 0.0-0.2
    
    // 眨眼
    float blink_frequency_multiplier;
    float blink_duration_multiplier;
    float blink_intensity;            // 眨眼程度 [0-1]
    
    // 眼皮
    float eyelid_upper_close;         // 上眼皮闭合度 [0-1]
    float eyelid_lower_raise;         // 下眼皮上抬度 [0-1]
    float eyelid_asymmetry;           // 左右不对称度 [0-1]
    float eyelid_squint_factor;       // 眯眼程度 [0-1]
    
    // 音效
    std::string audio_effect;         // 情绪对应的音效别名
    
    // 序列化为JSON
    nlohmann::json toJson() const;
};

/**
 * @class DynamicParameterGenerator
 * @brief 根据情绪、性格、历史生成眼睛参数
 * 
 * 流程:
 * 1. 从情绪预设获取基础参数
 * 2. 应用性格调节系数
 * 3. 应用历史趋势(最近是否持续该情绪)
 * 4. 应用连续维度微调
 */
class DynamicParameterGenerator {
public:
    DynamicParameterGenerator();
    
    /**
     * @brief 从配置文件加载参数预设和调节规则
     * @param preset_config 情绪预设配置文件路径
     * @param personality_config 性格调节规则路径
     */
    void loadFromConfig(const std::string& preset_config,
                       const std::string& personality_config);
    
    /**
     * @brief 生成眼睛参数
     * @param emotion 离散情绪分类结果
     * @param state 连续情绪状态
     * @param personality 性格模型
     * @param recent_history 最近采样历史
     */
    EyeParameters generateEyeParameters(
        const DiscreteEmotion& emotion,
        const EmotionState& state,
        const Personality& personality,
        const std::vector<float>& recent_valence_history,
        const std::vector<float>& recent_arousal_history
    );
    
private:
    std::map<std::string, EyeParameters> emotion_presets_;
    
    /**
     * @brief 应用性格调节
     */
    void applyPersonalityModulation(
        EyeParameters& params,
        const Personality& personality,
        const EmotionState& state
    );
    
    /**
     * @brief 计算历史趋势一致性
     */
    float calculateHistoryConsistency(
        const std::vector<float>& history,
        float current_value
    ) const;
    
    /**
     * @brief 计算标准差
     */
    float calculateStdDev(const std::vector<float>& values) const;
};

} // namespace eye_engine::emotion
