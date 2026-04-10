#pragma once

#include <string>
#include <vector>

namespace eye_engine::emotion {

struct EmotionState;

/**
 * @struct DiscreteEmotion
 * @brief 离散情绪分类结果
 */
struct DiscreteEmotion {
    std::string name;           // happy/sad/curious/confused/excited/tired/surprised/afraid
    float confidence;           // [0.0, 1.0] 置信度
    float primary_distance;     // 到该情绪原型的距离
    bool is_mixed;              // 是否混合情绪(同时接近多个原型)
    std::vector<std::string> secondary_emotions;  // 备选情绪
};

/**
 * @class EmotionDiscretizer
 * @brief 将连续情绪空间映射到离散情绪分类
 * 
 * 使用欧几里得距离在 (valence, arousal) 空间中
 * 找到最近的8种情绪原型
 */
class EmotionDiscretizer {
public:
    EmotionDiscretizer();
    
    /**
     * @brief 从配置文件加载情绪原型位置
     * @param config_path 通常为 config/emotion_dynamics.json
     */
    void loadFromConfig(const std::string& config_path);
    
    /**
     * @brief 将连续情绪状态分类到离散情绪
     * @param state 连续4维情绪状态
     * @return 离散情绪分类结果
     */
    DiscreteEmotion classify(const EmotionState& state);
    
private:
    struct EmotionPrototype {
        std::string name;
        float valence;
        float arousal;
    };
    
    std::vector<EmotionPrototype> prototypes_;
    float distance_threshold_;      // 同时接近多个情绪的阈值
    float confidence_min_threshold_;
    
    /**
     * @brief 计算两点间欧几里得距离
     */
    float euclideanDistance(float v1, float a1, float v2, float a2) const;
};

} // namespace eye_engine::emotion
