/**
 * @file test_eye_state.cpp
 * @brief EyeState 单元测试
 * 
 * 测试 SharedState 中眼睛状态的读写功能
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/shared_state.hpp"
#include "drive/shared_state_utils.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <cstring>

using namespace doly::drive;

// 测试辅助宏
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "❌ FAILED: " << message << std::endl; \
            return false; \
        } \
    } while(0)

#define TEST_PASS(message) \
    std::cout << "✅ PASSED: " << message << std::endl

/**
 * @brief 测试 1: EyeState 结构体大小
 */
bool Test_EyeStateSize() {
    std::cout << "\n=== Test 1: EyeState Size ===" << std::endl;
    
    SharedState state;
    size_t eye_size = sizeof(state.eye);
    
    std::cout << "Eye state size: " << eye_size << " bytes" << std::endl;
    std::cout << "Total SharedState size: " << sizeof(SharedState) << " bytes" << std::endl;
    
    // 验证大小合理（应该小于 128 字节）
    TEST_ASSERT(eye_size <= 128, "Eye state size should be <= 128 bytes");
    
    TEST_PASS("EyeState size is acceptable");
    return true;
}

/**
 * @brief 测试 2: 情绪写入和读取
 */
bool Test_EmotionReadWrite() {
    std::cout << "\n=== Test 2: Emotion Read/Write ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    // 写入情绪
    WriteEyeEmotion(&state, 1, 0.8f); // happy, 0.8
    
    // 读取快照
    auto snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.emotion == 1, "Emotion should be 1 (happy)");
    TEST_ASSERT(snapshot.intensity == 0.8f, "Intensity should be 0.8");
    TEST_ASSERT(snapshot.sequence == 1, "Sequence should be incremented");
    TEST_ASSERT(snapshot.update_time_ms > 0, "Update time should be set");
    
    TEST_PASS("Emotion read/write works correctly");
    return true;
}

/**
 * @brief 测试 3: 表情写入和读取
 */
bool Test_ExpressionReadWrite() {
    std::cout << "\n=== Test 3: Expression Read/Write ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    // 写入表情
    WriteEyeExpression(&state, 2); // wide
    
    // 读取快照
    auto snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.expression == 2, "Expression should be 2 (wide)");
    TEST_ASSERT(snapshot.sequence == 1, "Sequence should be incremented");
    
    TEST_PASS("Expression read/write works correctly");
    return true;
}

/**
 * @brief 测试 4: 凝视状态写入和读取
 */
bool Test_GazeReadWrite() {
    std::cout << "\n=== Test 4: Gaze Read/Write ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    // 写入凝视状态
    WriteEyeGaze(&state, 5, 0.5f, -0.3f); // up_left, x=0.5, y=-0.3
    
    // 读取快照
    auto snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.gaze_type == 5, "Gaze type should be 5 (up_left)");
    TEST_ASSERT(snapshot.gaze_x == 0.5f, "Gaze X should be 0.5");
    TEST_ASSERT(snapshot.gaze_y == -0.3f, "Gaze Y should be -0.3");
    
    TEST_PASS("Gaze read/write works correctly");
    return true;
}

/**
 * @brief 测试 5: Slot 状态写入和读取
 */
bool Test_SlotReadWrite() {
    std::cout << "\n=== Test 5: Slot Read/Write ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    // 写入左眼 Slot 状态
    WriteEyeSlot(&state, 0, 1, 1); // left, showing_widget, clock
    
    // 读取快照
    auto snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.left_slot_state == 1, "Left slot state should be 1 (showing_widget)");
    TEST_ASSERT(snapshot.left_widget_id == 1, "Left widget ID should be 1 (clock)");
    
    // 写入右眼 Slot 状态
    WriteEyeSlot(&state, 1, 1, 2); // right, showing_widget, weather
    
    snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.right_slot_state == 1, "Right slot state should be 1 (showing_widget)");
    TEST_ASSERT(snapshot.right_widget_id == 2, "Right widget ID should be 2 (weather)");
    
    TEST_PASS("Slot read/write works correctly");
    return true;
}

/**
 * @brief 测试 6: LCD 启用状态
 */
bool Test_LcdEnabledReadWrite() {
    std::cout << "\n=== Test 6: LCD Enabled Read/Write ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    // 写入 LCD 启用状态
    WriteEyeLcdEnabled(&state, false, true);
    
    // 读取快照
    auto snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.left_lcd_enabled == false, "Left LCD should be disabled");
    TEST_ASSERT(snapshot.right_lcd_enabled == true, "Right LCD should be enabled");
    
    TEST_PASS("LCD enabled read/write works correctly");
    return true;
}

/**
 * @brief 测试 7: 动画状态
 */
bool Test_AnimStateReadWrite() {
    std::cout << "\n=== Test 7: Animation State Read/Write ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    // 写入动画状态
    WriteEyeAnimState(&state, true, false);
    
    // 读取快照
    auto snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.is_blinking == true, "is_blinking should be true");
    TEST_ASSERT(snapshot.is_transitioning == false, "is_transitioning should be false");
    
    TEST_PASS("Animation state read/write works correctly");
    return true;
}

/**
 * @brief 测试 8: 性能指标
 */
bool Test_PerformanceReadWrite() {
    std::cout << "\n=== Test 8: Performance Read/Write ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    // 写入性能指标
    WriteEyePerformance(&state, 60, 12345);
    
    // 读取快照
    auto snapshot = ReadEyeState(&state);
    
    TEST_ASSERT(snapshot.fps == 60, "FPS should be 60");
    TEST_ASSERT(snapshot.frame_count == 12345, "Frame count should be 12345");
    TEST_ASSERT(snapshot.sequence == 0, "Sequence should NOT be incremented for performance updates");
    
    TEST_PASS("Performance read/write works correctly");
    return true;
}

/**
 * @brief 测试 9: 多线程并发访问
 */
bool Test_ConcurrentAccess() {
    std::cout << "\n=== Test 9: Concurrent Access ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    const int NUM_ITERATIONS = 1000;
    std::atomic<bool> writer_done{false};
    std::atomic<bool> reader_done{false};
    
    // 写线程
    std::thread writer([&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            WriteEyeEmotion(&state, i % 8, (i % 100) / 100.0f);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        writer_done = true;
    });
    
    // 读线程
    std::thread reader([&]() {
        int read_count = 0;
        while (!writer_done || read_count < NUM_ITERATIONS) {
            auto snapshot = ReadEyeState(&state);
            ++read_count;
            std::this_thread::sleep_for(std::chrono::microseconds(5));
        }
        reader_done = true;
    });
    
    writer.join();
    reader.join();
    
    TEST_ASSERT(writer_done && reader_done, "Both threads should complete");
    
    auto final_snapshot = ReadEyeState(&state);
    TEST_ASSERT(final_snapshot.sequence == NUM_ITERATIONS, "Sequence should match iteration count");
    
    TEST_PASS("Concurrent access is thread-safe");
    return true;
}

/**
 * @brief 测试 10: 读取性能测试
 */
bool Test_ReadPerformance() {
    std::cout << "\n=== Test 10: Read Performance ===" << std::endl;
    
    SharedState state;
    memset(&state, 0, sizeof(SharedState));
    
    const int NUM_READS = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_READS; ++i) {
        auto snapshot = ReadEyeState(&state);
        (void)snapshot; // 避免优化掉
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double avg_time_ns = duration / (double)NUM_READS;
    double avg_time_us = avg_time_ns / 1000.0;
    
    std::cout << "Average read time: " << avg_time_us << " μs" << std::endl;
    
    TEST_ASSERT(avg_time_us < 10.0, "Read time should be < 10μs");
    
    TEST_PASS("Read performance meets requirement");
    return true;
}

/**
 * @brief 主测试函数
 */
int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "  EyeState Unit Tests" << std::endl;
    std::cout << "==================================" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    // 运行所有测试
    if (Test_EyeStateSize()) ++passed; else ++failed;
    if (Test_EmotionReadWrite()) ++passed; else ++failed;
    if (Test_ExpressionReadWrite()) ++passed; else ++failed;
    if (Test_GazeReadWrite()) ++passed; else ++failed;
    if (Test_SlotReadWrite()) ++passed; else ++failed;
    if (Test_LcdEnabledReadWrite()) ++passed; else ++failed;
    if (Test_AnimStateReadWrite()) ++passed; else ++failed;
    if (Test_PerformanceReadWrite()) ++passed; else ++failed;
    if (Test_ConcurrentAccess()) ++passed; else ++failed;
    if (Test_ReadPerformance()) ++passed; else ++failed;
    
    // 汇总结果
    std::cout << "\n==================================" << std::endl;
    std::cout << "  Test Summary" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Total: " << (passed + failed) << std::endl;
    std::cout << "✅ Passed: " << passed << std::endl;
    std::cout << "❌ Failed: " << failed << std::endl;
    std::cout << "Coverage: " << (passed * 100 / (passed + failed)) << "%" << std::endl;
    
    return (failed == 0) ? 0 : 1;
}
