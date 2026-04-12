/**
 * @file shared_state_utils.cpp
 * @brief SharedState 读写工具实现
 * 
 * 提供便捷的原子读写接口
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/shared_state.hpp"
#include <cstring>

namespace doly {
namespace drive {

/**
 * @brief 眼睛状态快照（用于批量读取）
 * 避免多次原子操作
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
 * @param state SharedState 指针
 * @return EyeStateSnapshot 快照数据
 */
EyeStateSnapshot ReadEyeState(const SharedState* state) {
    EyeStateSnapshot snapshot;
    
    // 批量原子读取
    snapshot.emotion = state->eye.emotion.load(std::memory_order_relaxed);
    snapshot.intensity = state->eye.intensity.load(std::memory_order_relaxed);
    snapshot.expression = state->eye.expression.load(std::memory_order_relaxed);
    snapshot.gaze_type = state->eye.gaze_type.load(std::memory_order_relaxed);
    snapshot.gaze_x = state->eye.gaze_x.load(std::memory_order_relaxed);
    snapshot.gaze_y = state->eye.gaze_y.load(std::memory_order_relaxed);
    snapshot.left_slot_state = state->eye.left_slot_state.load(std::memory_order_relaxed);
    snapshot.right_slot_state = state->eye.right_slot_state.load(std::memory_order_relaxed);
    snapshot.left_widget_id = state->eye.left_widget_id.load(std::memory_order_relaxed);
    snapshot.right_widget_id = state->eye.right_widget_id.load(std::memory_order_relaxed);
    snapshot.left_lcd_enabled = state->eye.left_lcd_enabled.load(std::memory_order_relaxed);
    snapshot.right_lcd_enabled = state->eye.right_lcd_enabled.load(std::memory_order_relaxed);
    snapshot.is_blinking = state->eye.is_blinking.load(std::memory_order_relaxed);
    snapshot.is_transitioning = state->eye.is_transitioning.load(std::memory_order_relaxed);
    snapshot.fps = state->eye.fps.load(std::memory_order_relaxed);
    snapshot.frame_count = state->eye.frame_count.load(std::memory_order_relaxed);
    snapshot.update_time_ms = state->eye.update_time_ms.load(std::memory_order_relaxed);
    snapshot.sequence = state->eye.sequence.load(std::memory_order_acquire); // 使用 acquire 确保可见性
    
    return snapshot;
}

/**
 * @brief 原子写入眼睛情绪
 * @param state SharedState 指针
 * @param emotion 情绪类型
 * @param intensity 强度
 */
void WriteEyeEmotion(SharedState* state, uint8_t emotion, float intensity) {
    state->eye.emotion.store(emotion, std::memory_order_relaxed);
    state->eye.intensity.store(intensity, std::memory_order_relaxed);
    state->eye.update_time_ms.store(SharedState::getCurrentTimeMs(), std::memory_order_relaxed);
    state->eye.sequence.fetch_add(1, std::memory_order_release); // 使用 release 确保写入顺序
}

/**
 * @brief 原子写入眼睛表情
 * @param state SharedState 指针
 * @param expression 表情类型
 */
void WriteEyeExpression(SharedState* state, uint8_t expression) {
    state->eye.expression.store(expression, std::memory_order_relaxed);
    state->eye.update_time_ms.store(SharedState::getCurrentTimeMs(), std::memory_order_relaxed);
    state->eye.sequence.fetch_add(1, std::memory_order_release);
}

/**
 * @brief 原子写入凝视状态
 * @param state SharedState 指针
 * @param gaze_type 凝视类型
 * @param x X 坐标
 * @param y Y 坐标
 */
void WriteEyeGaze(SharedState* state, uint8_t gaze_type, float x, float y) {
    state->eye.gaze_type.store(gaze_type, std::memory_order_relaxed);
    state->eye.gaze_x.store(x, std::memory_order_relaxed);
    state->eye.gaze_y.store(y, std::memory_order_relaxed);
    state->eye.update_time_ms.store(SharedState::getCurrentTimeMs(), std::memory_order_relaxed);
    state->eye.sequence.fetch_add(1, std::memory_order_release);
}

/**
 * @brief 原子写入 Slot 状态
 * @param state SharedState 指针
 * @param slot 0=left, 1=right
 * @param slot_state Slot 状态
 * @param widget_id Widget ID
 */
void WriteEyeSlot(SharedState* state, uint8_t slot, uint8_t slot_state, uint8_t widget_id) {
    if (slot == 0) {
        state->eye.left_slot_state.store(slot_state, std::memory_order_relaxed);
        state->eye.left_widget_id.store(widget_id, std::memory_order_relaxed);
    } else {
        state->eye.right_slot_state.store(slot_state, std::memory_order_relaxed);
        state->eye.right_widget_id.store(widget_id, std::memory_order_relaxed);
    }
    state->eye.update_time_ms.store(SharedState::getCurrentTimeMs(), std::memory_order_relaxed);
    state->eye.sequence.fetch_add(1, std::memory_order_release);
}

/**
 * @brief 原子写入 LCD 启用状态
 * @param state SharedState 指针
 * @param left_enabled 左眼 LCD 启用
 * @param right_enabled 右眼 LCD 启用
 */
void WriteEyeLcdEnabled(SharedState* state, bool left_enabled, bool right_enabled) {
    state->eye.left_lcd_enabled.store(left_enabled, std::memory_order_relaxed);
    state->eye.right_lcd_enabled.store(right_enabled, std::memory_order_relaxed);
    state->eye.update_time_ms.store(SharedState::getCurrentTimeMs(), std::memory_order_relaxed);
    state->eye.sequence.fetch_add(1, std::memory_order_release);
}

/**
 * @brief 原子写入动画状态
 * @param state SharedState 指针
 * @param is_blinking 正在眨眼
 * @param is_transitioning 正在转场
 */
void WriteEyeAnimState(SharedState* state, bool is_blinking, bool is_transitioning) {
    state->eye.is_blinking.store(is_blinking, std::memory_order_relaxed);
    state->eye.is_transitioning.store(is_transitioning, std::memory_order_relaxed);
    state->eye.update_time_ms.store(SharedState::getCurrentTimeMs(), std::memory_order_relaxed);
    state->eye.sequence.fetch_add(1, std::memory_order_release);
}

/**
 * @brief 原子写入性能指标
 * @param state SharedState 指针
 * @param fps 帧率
 * @param frame_count 总帧数
 */
void WriteEyePerformance(SharedState* state, uint16_t fps, uint64_t frame_count) {
    state->eye.fps.store(fps, std::memory_order_relaxed);
    state->eye.frame_count.store(frame_count, std::memory_order_relaxed);
    state->eye.update_time_ms.store(SharedState::getCurrentTimeMs(), std::memory_order_relaxed);
    // 性能指标更新不增加 sequence（避免频繁触发）
}

} // namespace drive
} // namespace doly
