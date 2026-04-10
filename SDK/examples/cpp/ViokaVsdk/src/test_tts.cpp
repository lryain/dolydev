/**
 * @file test_tts.cpp
 * @brief TTS 测试 - Vsdk::Tts::Events 真实数据结构测试
 *
 * 说明:
 *   此 SDK 包不含 TTS 引擎的具体实现（无 libvsdk-tts*.so），
 *   因此无法直接实例化 Vsdk::Tts::Engine 或调用 synthesizeFromText()。
 *
 *   本测试对可用的具体结构体进行真实测试：
 *   - Vsdk::Tts::Events::Marker      (可构造 + JSON 序列化)
 *   - Vsdk::Tts::Events::WordMarker  (可构造 + JSON 序列化)
 */

#include <vsdk/tts/Events.hpp>
#include <vsdk/audio/Buffer.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cassert>
#include <memory>
#include <string>

extern std::shared_ptr<spdlog::logger> get_logger();

void test_tts()
{
    auto logger = get_logger();

    logger->info("================================================");
    logger->info("  TTS 测试 - Vsdk::Tts::Events 结构体验证");
    logger->info("================================================");
    logger->warn("  ⚠ 无具体 TTS 引擎实现（此 SDK 未包含 libvsdk-tts*.so）");
    logger->info("  本测试验证可用的具体数据结构和 JSON 序列化。");

    // ─────────────────────────────────────────────────────────
    // 1. Vsdk::Tts::Events::Marker 构造与 JSON 序列化
    // ─────────────────────────────────────────────────────────
    logger->info("[1] Vsdk::Tts::Events::Marker 构造");
    {
        Vsdk::Tts::Events::Marker m1;
        assert(m1.index      == 0);
        assert(m1.name.empty());
        assert(m1.posInAudio == 0);
        assert(m1.posInText  == 0);
        logger->info("  ✓ 默认构造: index={} name=\"{}\" posInAudio={} posInText={}",
                     m1.index, m1.name, m1.posInAudio, m1.posInText);

        Vsdk::Tts::Events::Marker m2(3, "MARK_intro", 24000, 7);
        assert(m2.index      == 3);
        assert(m2.name       == "MARK_intro");
        assert(m2.posInAudio == 24000);
        assert(m2.posInText  == 7);
        logger->info("  ✓ 带参构造: index={} name=\"{}\" posInAudio={} posInText={}",
                     m2.index, m2.name, m2.posInAudio, m2.posInText);
    }

    // ─────────────────────────────────────────────────────────
    // 2. Marker JSON 序列化 / 反序列化
    // ─────────────────────────────────────────────────────────
    logger->info("[2] Marker JSON 序列化 / 反序列化");
    {
        Vsdk::Tts::Events::Marker original(1, "chapter_start", 16000, 0);

        // to_json
        nlohmann::json j;
        Vsdk::Tts::Events::to_json(j, original);
        logger->info("  ✓ to_json: {}", j.dump());

        // from_json (round-trip)
        Vsdk::Tts::Events::Marker restored;
        Vsdk::Tts::Events::from_json(j, restored);

        assert(restored.index      == original.index);
        assert(restored.name       == original.name);
        assert(restored.posInAudio == original.posInAudio);
        assert(restored.posInText  == original.posInText);
        logger->info("  ✓ from_json 还原: index={} name=\"{}\" posInAudio={} posInText={}",
                     restored.index, restored.name, restored.posInAudio, restored.posInText);
    }

    // ─────────────────────────────────────────────────────────
    // 3. WordMarker 构造与 JSON 序列化
    // ─────────────────────────────────────────────────────────
    logger->info("[3] Vsdk::Tts::Events::WordMarker 构造");
    {
        Vsdk::Tts::Events::WordMarker wm1;
        assert(wm1.index == 0);
        assert(wm1.text.empty());
        logger->info("  ✓ 默认构造: index={} text=\"{}\"", wm1.index, wm1.text);

        Vsdk::Tts::Events::WordMarker wm2(2, "Hello world", "Hello", 0, 8000, 0, 5);
        assert(wm2.index         == 2);
        assert(wm2.text          == "Hello world");
        assert(wm2.word          == "Hello");
        assert(wm2.startPosInAudio == 0);
        assert(wm2.endPosInAudio   == 8000);
        logger->info("  ✓ 带参构造: index={} word=\"{}\" audio=[{},{}] text=[{},{}]",
                     wm2.index, wm2.word,
                     wm2.startPosInAudio, wm2.endPosInAudio,
                     wm2.startPosInText, wm2.endPosInText);
    }

    // ─────────────────────────────────────────────────────────
    // 4. WordMarker JSON 序列化 / 反序列化
    // ─────────────────────────────────────────────────────────
    logger->info("[4] WordMarker JSON 序列化 / 反序列化");
    {
        Vsdk::Tts::Events::WordMarker wm(0, "你好世界", "你好", 0, 12000, 0, 2);

        nlohmann::json j;
        Vsdk::Tts::Events::to_json(j, wm);
        logger->info("  ✓ to_json: {}", j.dump());

        Vsdk::Tts::Events::WordMarker wm2;
        Vsdk::Tts::Events::from_json(j, wm2);

        assert(wm2.text == wm.text);
        assert(wm2.word == wm.word);
        assert(wm2.endPosInAudio == wm.endPosInAudio);
        logger->info("  ✓ from_json 还原: word=\"{}\" text=\"{}\" endPosInAudio={}",
                     wm2.word, wm2.text, wm2.endPosInAudio);
    }

    // ─────────────────────────────────────────────────────────
    // 5. Audio::Buffer 与 TTS 输出的兼容性
    //    (TTS 引擎将返回 Vsdk::Audio::Buffer, 这里验证 Buffer 接收)
    // ─────────────────────────────────────────────────────────
    logger->info("[5] Audio::Buffer 接收模拟 TTS 输出");
    {
        // 模拟 TTS 引擎返回 22050 Hz 单声道 PCM
        Vsdk::Audio::Buffer fakeTtsOutput(22050, 1);
        std::vector<int16_t> synth(4410, 0);  // 0.2 s 静音
        fakeTtsOutput.append(synth);

        assert(fakeTtsOutput.sampleRate()   == 22050);
        assert(fakeTtsOutput.channelCount() == 1);
        assert(fakeTtsOutput.size()         == 4410);
        logger->info("  ✓ 模拟 TTS PCM: sampleRate={} channelCount={} size={}",
                     fakeTtsOutput.sampleRate(),
                     fakeTtsOutput.channelCount(),
                     fakeTtsOutput.size());
    }

    logger->info("================================================");
    logger->info("  TTS 结构体测试完成");
    logger->info("================================================");
}
