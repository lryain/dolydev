#pragma once

#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace eye_engine::emotion {

/**
 * @class EmotionState
 * @brief 连续4维情绪状态表示
 * 
 * 基于 Russell 情感循环模型的扩展：
 * - valence: [-1.0, 1.0] 情感效价 (不愉悦←→愉悦)
 * - arousal: [0.0, 1.0]  唤醒度 (沉睡←→激活)
 * - energy:  [0.0, 1.0]  精力值
 * - tension: [0.0, 1.0]  紧张度 (影响眨眼、眼皮)
 */
struct EmotionState {
    float valence;      // 情感效价
    float arousal;      // 唤醒度
    float energy;       // 精力值
    float tension;      // 紧张度
    
    // 内部参数
    float decay_factor; // 衰减速率 [0.0, 1.0]
    float momentum;     // 惯性系数 [0.0, 1.0]
    uint64_t timestamp; // 上次更新时间戳(ms)
    
    // 历史数据 (用于趋势计算)
    std::vector<float> valence_history;  // 最近采样值
    std::vector<float> arousal_history;
    
    static const int MAX_HISTORY = 100;
    static const int HISTORY_SAMPLE_INTERVAL_MS = 100;
    
    // 默认构造
    EmotionState()
        : valence(0.0f), arousal(0.3f), energy(0.8f), tension(0.0f),
          decay_factor(0.2f), momentum(0.3f), timestamp(0) {}
    
    /**
     * @brief 情绪衰减 - 随时间向中立状态回归
     * @param dt 时间增量(秒)
     */
    void decay(float dt);
    
    /**
     * @brief 应用外部刺激
     * @param delta_valence 
     * @param delta_arousal
     * @param delta_energy
     */
    void applyStimulus(float delta_valence, float delta_arousal, float delta_energy);
    
    /**
     * @brief 参数范围约束
     */
    void clamp();
    
    /**
     * @brief 记录历史采样点
     */
    void recordHistory();
    
    /**
     * @brief 计算最近N个采样的标准差(用于趋势检测)
     */
    float calculateValenceTrend(int lookback_samples = 10) const;
    float calculateArousalTrend(int lookback_samples = 10) const;
};

} // namespace eye_engine::emotion
