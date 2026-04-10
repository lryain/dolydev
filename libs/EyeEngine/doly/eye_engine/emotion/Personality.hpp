#pragma once

namespace eye_engine::emotion {

/**
 * @struct Personality
 * @brief 5维性格模型
 * 
 * 影响情绪的表现方式、动态特性、与外界交互的强度
 */
struct Personality {
    float extraversion;  // 外向度 [0.0, 1.0] - 影响反应强度
    float curiosity;     // 好奇心 [0.0, 1.0] - 影响探索倾向
    float patience;      // 耐心 [0.0, 1.0]   - 影响情绪衰减速度
    float humor;         // 幽默感 [0.0, 1.0] - 影响快乐阈值
    float empathy;       // 共鸣度 [0.0, 1.0] - 影响对用户情绪的敏感度
    
    // 默认为"开朗外向型"
    Personality()
        : extraversion(0.95f),
          curiosity(0.85f),
          patience(0.55f),
          humor(0.95f),
          empathy(0.75f) {}
    
    /**
     * @brief 从预设加载性格
     * @param preset_name 预设名: cheerful_extrovert/calm_introvert/curious_explorer/...
     */
    static Personality fromPreset(const std::string& preset_name);
    
    /**
     * @brief 性格参数范围约束
     */
    void clamp();
};

} // namespace eye_engine::emotion
