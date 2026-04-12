/**
 * @file shared_state_utils.hpp
 * @brief SharedState 读写工具接口
 * 
 * 提供便捷的原子读写接口
 */

#pragma once

#include "drive/shared_state.hpp"

namespace doly {
namespace drive {

/**
 * @brief 眼睛状态快照（用于批量读取）
 */
struct EyeStateSnapshot {
    uint8_t emotion;
    float intensity;
    uint8_t expression;
    uint8_t gaze_type;
    float gaze_x;
    float gaze_y;
    uint8_t left_slot_state;
    uint8_t right_slot_state;
    uint8_t left_widget_id;
    uint8_t right_widget_id;
    bool left_lcd_enabled;
    bool right_lcd_enabled;
    bool is_blinking;
    bool is_transitioning;
    uint16_t fps;
    uint64_t frame_count;
    uint64_t update_time_ms;
    uint32_t sequence;
};

/**
 * @brief 原子读取眼睛状态快照
 */
EyeStateSnapshot ReadEyeState(const SharedState* state);

/**
 * @brief 原子写入眼睛情绪
 */
void WriteEyeEmotion(SharedState* state, uint8_t emotion, float intensity);

/**
 * @brief 原子写入眼睛表情
 */
void WriteEyeExpression(SharedState* state, uint8_t expression);

/**
 * @brief 原子写入凝视状态
 */
void WriteEyeGaze(SharedState* state, uint8_t gaze_type, float x, float y);

/**
 * @brief 原子写入 Slot 状态
 */
void WriteEyeSlot(SharedState* state, uint8_t slot, uint8_t slot_state, uint8_t widget_id);

/**
 * @brief 原子写入 LCD 启用状态
 */
void WriteEyeLcdEnabled(SharedState* state, bool left_enabled, bool right_enabled);

/**
 * @brief 原子写入动画状态
 */
void WriteEyeAnimState(SharedState* state, bool is_blinking, bool is_transitioning);

/**
 * @brief 原子写入性能指标
 */
void WriteEyePerformance(SharedState* state, uint16_t fps, uint64_t frame_count);

} // namespace drive
} // namespace doly
