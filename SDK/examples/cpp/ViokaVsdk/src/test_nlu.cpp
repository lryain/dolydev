/**
 * @file test_nlu.cpp
 * @brief NLU 测试 - Vsdk::Nlu::Result 真实数据结构测试
 *
 * 说明:
 *   此 SDK 包不含 NLU 引擎的具体实现（无 libvsdk-nlu*.so），
 *   因此无法直接实例化 Vsdk::Nlu::Engine 或调用 Parser::process()。
 *
 *   本测试对可用的具体结构体进行真实测试：
 *   - Vsdk::Nlu::Intent   (可构造)
 *   - Vsdk::Nlu::Entity   (可构造)
 *   - Vsdk::Nlu::Result   (可构造)
 *   - to_json / from_json (ResultSerialization.hpp - JSON 序列化)
 */

#include <vsdk/nlu/Result.hpp>
#include <vsdk/nlu/ResultSerialization.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cassert>
#include <cmath>
#include <memory>
#include <string>

extern std::shared_ptr<spdlog::logger> get_logger();

void test_nlu()
{
    auto logger = get_logger();

    logger->info("================================================");
    logger->info("  NLU 测试 - Vsdk::Nlu 结构体验证");
    logger->info("================================================");
    logger->warn("  ⚠ 无具体 NLU 引擎实现（此 SDK 未包含 libvsdk-nlu*.so）");
    logger->info("  本测试验证可用的具体数据结构和 JSON 序列化。");

    // ─────────────────────────────────────────────────────────
    // 1. Vsdk::Nlu::Intent 构造
    // ─────────────────────────────────────────────────────────
    logger->info("[1] Vsdk::Nlu::Intent 构造");
    {
        Vsdk::Nlu::Intent intent1;
        assert(!intent1.name.has_value());
        assert(intent1.confidence == 0.f);
        logger->info("  ✓ 默认 Intent: name=nullopt confidence={}", intent1.confidence);

        Vsdk::Nlu::Intent intent2;
        intent2.name       = "播放音乐";
        intent2.confidence = 0.97f;
        assert(intent2.name.has_value());
        assert(intent2.name.value() == "播放音乐");
        assert(std::fabs(intent2.confidence - 0.97f) < 1e-5f);
        logger->info("  ✓ 赋值 Intent: name=\"{}\" confidence={:.4f}",
                     intent2.name.value(), intent2.confidence);
    }

    // ─────────────────────────────────────────────────────────
    // 2. Vsdk::Nlu::Entity 构造
    // ─────────────────────────────────────────────────────────
    logger->info("[2] Vsdk::Nlu::Entity 构造");
    {
        Vsdk::Nlu::Entity ent;
        ent.name       = "song_name";
        ent.value      = "爱你";
        ent.confidence = 0.88f;
        ent.startIndex = 3;
        ent.endIndex   = 5;

        assert(ent.name       == "song_name");
        assert(ent.value      == "爱你");
        assert(ent.startIndex == 3);
        assert(ent.endIndex   == 5);
        logger->info("  ✓ Entity: name=\"{}\" value=\"{}\" conf={:.2f} [{},{}]",
                     ent.name, ent.value, ent.confidence, ent.startIndex, ent.endIndex);
    }

    // ─────────────────────────────────────────────────────────
    // 3. Vsdk::Nlu::Result 构造（含多个 Entity）
    // ─────────────────────────────────────────────────────────
    logger->info("[3] Vsdk::Nlu::Result 完整构造");
    {
        Vsdk::Nlu::Result r;
        r.lang             = "zh-CN";
        r.originalSentence = "播放周杰伦的稻香";
        r.intent.name      = "play_music";
        r.intent.confidence = 0.95f;

        Vsdk::Nlu::Entity artist;
        artist.name       = "artist";
        artist.value      = "周杰伦";
        artist.confidence = 0.93f;
        artist.startIndex = 2;
        artist.endIndex   = 5;
        r.entities.push_back(artist);

        Vsdk::Nlu::Entity song;
        song.name       = "song";
        song.value      = "稻香";
        song.confidence = 0.91f;
        song.startIndex = 6;
        song.endIndex   = 8;
        r.entities.push_back(song);

        assert(r.lang             == "zh-CN");
        assert(r.originalSentence == "播放周杰伦的稻香");
        assert(r.intent.name.value() == "play_music");
        assert(r.entities.size()  == 2);
        assert(r.entities[0].name == "artist");
        assert(r.entities[1].value == "稻香");

        logger->info("  ✓ Result: lang={} sentence=\"{}\" intent=\"{}\" ({} entities)",
                     r.lang, r.originalSentence, r.intent.name.value(), r.entities.size());
        for (auto const & e : r.entities) {
            logger->info("    Entity: {}=\"{}\" conf={:.2f} [{},{}]",
                         e.name, e.value, e.confidence, e.startIndex, e.endIndex);
        }
    }

    // ─────────────────────────────────────────────────────────
    // 4. JSON 序列化 / 反序列化 - Intent
    // ─────────────────────────────────────────────────────────
    logger->info("[4] Intent JSON 序列化 / 反序列化");
    {
        Vsdk::Nlu::Intent original;
        original.name       = "turn_on_light";
        original.confidence = 0.82f;

        nlohmann::json j;
        Vsdk::Nlu::to_json(j, original);
        logger->info("  ✓ to_json: {}", j.dump());

        Vsdk::Nlu::Intent restored;
        Vsdk::Nlu::from_json(j, restored);

        assert(restored.name.has_value());
        assert(restored.name.value() == "turn_on_light");
        assert(std::fabs(restored.confidence - 0.82f) < 1e-4f);
        logger->info("  ✓ from_json: name=\"{}\" confidence={:.4f}",
                     restored.name.value(), restored.confidence);
    }

    // ─────────────────────────────────────────────────────────
    // 5. JSON 序列化 / 反序列化 - Entity
    // ─────────────────────────────────────────────────────────
    logger->info("[5] Entity JSON 序列化 / 反序列化");
    {
        Vsdk::Nlu::Entity original;
        original.name       = "location";
        original.value      = "上海";
        original.confidence = 0.76f;
        original.startIndex = 4;
        original.endIndex   = 6;

        nlohmann::json j;
        Vsdk::Nlu::to_json(j, original);
        logger->info("  ✓ to_json: {}", j.dump());

        Vsdk::Nlu::Entity restored;
        Vsdk::Nlu::from_json(j, restored);

        assert(restored.name       == original.name);
        assert(restored.value      == original.value);
        assert(restored.startIndex == original.startIndex);
        assert(restored.endIndex   == original.endIndex);
        logger->info("  ✓ from_json: name=\"{}\" value=\"{}\" [{},{}]",
                     restored.name, restored.value, restored.startIndex, restored.endIndex);
    }

    // ─────────────────────────────────────────────────────────
    // 6. JSON 序列化 / 反序列化 - 完整 Result（round-trip）
    // ─────────────────────────────────────────────────────────
    logger->info("[6] Result JSON 完整 round-trip");
    {
        Vsdk::Nlu::Result original;
        original.lang             = "en-US";
        original.originalSentence = "set timer for 5 minutes";
        original.intent.name      = "set_timer";
        original.intent.confidence = 0.99f;

        Vsdk::Nlu::Entity dur;
        dur.name       = "duration";
        dur.value      = "5 minutes";
        dur.confidence = 0.98f;
        dur.startIndex = 15;
        dur.endIndex   = 24;
        original.entities.push_back(dur);

        // 序列化
        nlohmann::json j;
        Vsdk::Nlu::to_json(j, original);
        logger->info("  ✓ to_json: {}", j.dump());

        // 反序列化
        Vsdk::Nlu::Result restored;
        Vsdk::Nlu::from_json(j, restored);

        assert(restored.lang             == original.lang);
        assert(restored.originalSentence == original.originalSentence);
        assert(restored.intent.name.value() == original.intent.name.value());
        assert(std::fabs(restored.intent.confidence - original.intent.confidence) < 1e-4f);
        assert(restored.entities.size()  == 1);
        assert(restored.entities[0].name  == "duration");
        assert(restored.entities[0].value == "5 minutes");

        logger->info("  ✓ from_json: lang={} sentence=\"{}\" intent=\"{}\" conf={:.4f}",
                     restored.lang, restored.originalSentence,
                     restored.intent.name.value(), restored.intent.confidence);
        logger->info("    Entity[0]: {}=\"{}\" [{},{}]",
                     restored.entities[0].name, restored.entities[0].value,
                     restored.entities[0].startIndex, restored.entities[0].endIndex);
    }

    // ─────────────────────────────────────────────────────────
    // 7. Result - 无 intent 的情况（intent.name 为 nullopt）
    // ─────────────────────────────────────────────────────────
    logger->info("[7] Result - 无 intent (name=nullopt)");
    {
        Vsdk::Nlu::Result r;
        r.lang             = "fr-FR";
        r.originalSentence = "bonjour";
        r.intent.confidence = 0.0f;
        // name 不赋值，保持 nullopt

        assert(!r.intent.name.has_value());
        logger->info("  ✓ intent.name.has_value()={} confidence={}",
                     r.intent.name.has_value(), r.intent.confidence);

        // 序列化含 nullopt 的 Result
        nlohmann::json j;
        Vsdk::Nlu::to_json(j, r);
        logger->info("  ✓ to_json (nullopt name): {}", j.dump());
    }

    logger->info("================================================");
    logger->info("  NLU 结构体测试全部通过");
    logger->info("================================================");
}
