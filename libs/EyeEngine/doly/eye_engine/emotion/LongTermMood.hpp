#pragma once

namespace eye_engine::emotion {

struct Personality;
struct EmotionState;

/**
 * @class LongTermMood
 * @brief 长期基调管理系统
 * 
 * 用途:
 * 1. 根据性格设置默认的情感基调
 * 2. 让情绪随时间衰减向基调回归
 * 3. 维持角色的个性稳定性
 * 
 * 例: 开朗外向型 Doly 的默认 valence=0.7
 *     沉静内向型 Doly 的默认 valence=0.2
 */
class LongTermMood {
public:
    LongTermMood();
    
    /**
     * @brief 从性格初始化长期基调
     */
    void initializeFromPersonality(const Personality& personality);
    
    /**
     * @brief 获取默认的 valence (情感基调)
     */
    float getDefaultValence() const { return default_valence_; }
    
    /**
     * @brief 获取默认的 arousal (活跃度)
     */
    float getDefaultArousal() const { return default_arousal_; }
    
    /**
     * @brief 情绪向基调衰减
     * @param current_valence 当前 valence
     * @param dt 时间增量(秒)
     * @return 衰减后的新 valence
     */
    float decayTowardBaseline(float current_valence, float dt) const;
    
    /**
     * @brief 计算衰减因子(用于公式: new_E = old_E + (baseline - old_E) * factor)
     */
    float calculateDecayFactor(float dt, float decay_speed = 0.05f) const;
    
private:
    float default_valence_;  // 默认情感效价
    float default_arousal_;  // 默认唤醒度
};

} // namespace eye_engine::emotion
