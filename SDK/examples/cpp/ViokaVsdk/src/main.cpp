/**
 * @file main.cpp
 * @brief Vivoka VSDK API 综合测试例子
 * 
 * 本程序演示如何使用 Vivoka VSDK 库的各个主要 API：
 * - ASR (自动语音识别)
 * - TTS (文本转语音)
 * - NLU (自然语言理解)
 * - Audio (音频处理)
 */

#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Test function declarations
void test_asr();
void test_tts();
void test_nlu();
void test_audio();

/**
 * @brief 初始化日志系统
 */
void setup_logging() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::debug);
    
    std::vector<spdlog::sink_ptr> sinks{console_sink};
    auto logger = std::make_shared<spdlog::logger>("vivoka", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    
    spdlog::register_logger(logger);
}

/**
 * @brief 获取当前的日志记录器
 */
std::shared_ptr<spdlog::logger> get_logger() {
    return spdlog::get("vivoka");
}

int main(int argc, char* argv[]) {
    setup_logging();
    auto logger = get_logger();
    
    logger->info("======================================");
    logger->info("  Vivoka VSDK API Test Suite");
    logger->info("======================================");
    
    try {
        // Test ASR (自动语音识别)
        logger->info("\n[1/4] Testing ASR (自动语音识别)...");
        test_asr();
        logger->info("✓ ASR test completed");
        
        // Test TTS (文本转语音)
        logger->info("\n[2/4] Testing TTS (文本转语音)...");
        test_tts();
        logger->info("✓ TTS test completed");
        
        // Test NLU (自然语言理解)
        logger->info("\n[3/4] Testing NLU (自然语言理解)...");
        test_nlu();
        logger->info("✓ NLU test completed");
        
        // Test Audio (音频处理)
        logger->info("\n[4/4] Testing Audio (音频处理)...");
        test_audio();
        logger->info("✓ Audio test completed");
        
        logger->info("\n======================================");
        logger->info("  All tests completed successfully!");
        logger->info("======================================\n");
        
    } catch (const std::exception& e) {
        logger->error("Test failed with exception: {}", e.what());
        return 1;
    }
    
    return 0;
}
