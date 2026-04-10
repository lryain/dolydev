/**
 * @file test_asr.cpp
 * @brief ASR (自动语音识别) 真实测试 - 使用 Vsdk::Asr::Csdk::Engine
 *
 * 使用真实引擎、真实配置文件和真实 API 调用，无任何 dummy。
 */

#include <vsdk/asr/csdk/Engine.hpp>
#include <vsdk/asr/Engine.hpp>
#include <vsdk/asr/Recognizer.hpp>
#include <vsdk/audio/Buffer.hpp>
#include <vsdk/details/vsdk/StatusReporter.hpp>

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

extern std::shared_ptr<spdlog::logger> get_logger();

static const std::string SPEECH_CONFIG = "/.doly/config/speech/speech.json";

/**
 * @brief 测试 ASR 引擎 - 使用真实的 Vsdk::Asr::Csdk::Engine
 */
void test_asr() {
    auto logger = get_logger();

    logger->info("================================================");
    logger->info("  ASR 真实测试 - Vsdk::Asr::Csdk::Engine");
    logger->info("================================================");

    // ─────────────────────────────────────────────────────────
    // 1. 初始化真实引擎
    // ─────────────────────────────────────────────────────────
    logger->info("[1] 初始化 ASR 引擎，配置: {}", SPEECH_CONFIG);

    std::shared_ptr<Vsdk::Asr::Engine> engine;
    try {
        engine = Vsdk::Asr::Engine::make<Vsdk::Asr::Csdk::Engine>(SPEECH_CONFIG);
    } catch (std::exception const & e) {
        logger->error("引擎初始化失败: {}", e.what());
        throw;
    }

    logger->info("  ✓ 引擎版本: {}", engine->version());

    // ─────────────────────────────────────────────────────────
    // 2. 查看真实模型信息
    // ─────────────────────────────────────────────────────────
    logger->info("[2] 读取模型信息 (engine->modelsInfo())");
    auto const & models = engine->modelsInfo();
    logger->info("  ✓ 共加载 {} 个模型", models.size());

    for (auto const & [name, info] : models) {
        std::string typeStr;
        switch (info.type) {
            case Vsdk::Asr::Engine::ModelInfo::Type::Grammar:    typeStr = "Grammar";    break;
            case Vsdk::Asr::Engine::ModelInfo::Type::FreeSpeech: typeStr = "FreeSpeech"; break;
            case Vsdk::Asr::Engine::ModelInfo::Type::Dictation:  typeStr = "Dictation";  break;
            default:                                              typeStr = "Unknown";
        }
        logger->info("  模型: {:12s}  type={:10s}  lang={}", name, typeStr, info.language);
    }

    // ─────────────────────────────────────────────────────────
    // 3. 查看真实识别器信息
    // ─────────────────────────────────────────────────────────
    logger->info("[3] 读取识别器信息 (engine->recognizersInfo())");
    auto const & recs = engine->recognizersInfo();
    logger->info("  ✓ 共 {} 个识别器", recs.size());

    for (auto const & [name, info] : recs) {
        std::string langs;
        for (auto const & l : info.languages) {
            if (!langs.empty()) langs += ", ";
            langs += l;
        }
        logger->info("  识别器: {:12s}  语言: [{}]", name, langs);
    }

    // ─────────────────────────────────────────────────────────
    // 4. 获取识别器实例并订阅事件
    // ─────────────────────────────────────────────────────────
    // 选择第一个可用的识别器 + 模型
    if (recs.empty() || models.empty()) {
        logger->warn("  没有可用的识别器或模型，跳过 process 测试");
        return;
    }

    std::string recName   = recs.begin()->first;
    std::string modelName = models.begin()->first;
    logger->info("[4] 获取识别器 \"{}\"", recName);

    auto recognizer = engine->recognizer(recName);
    logger->info("  ✓ name()  = {}", recognizer->name());

    // 订阅事件回调
    std::atomic<int>  eventCount{0};
    std::mutex        eventMu;
    std::vector<std::string> eventLog;

    using EventCallback  = Vsdk::details::StatusReporter<Vsdk::Asr::RecognizerEventCode,
                                                          Vsdk::Asr::RecognizerErrorCode>::EventCallback;
    using ResultCallback = Vsdk::details::StatusReporter<Vsdk::Asr::RecognizerEventCode,
                                                          Vsdk::Asr::RecognizerErrorCode>::ResultCallback;
    using ErrorCallback  = Vsdk::details::StatusReporter<Vsdk::Asr::RecognizerEventCode,
                                                          Vsdk::Asr::RecognizerErrorCode>::ErrorCallback;

    recognizer->subscribe(EventCallback{
        [&](Vsdk::details::StatusEvent<Vsdk::Asr::RecognizerEventCode> const & evt) {
            std::lock_guard<std::mutex> lock(eventMu);
            eventLog.push_back(evt.codeString);
            ++eventCount;
        }
    });

    // 订阅结果回调
    std::atomic<bool> gotResult{false};
    recognizer->subscribe(ResultCallback{
        [&](Vsdk::details::StatusResult const & result) {
            logger->info("  [结果] isFinal={} json={}",
                         result.isFinal, result.json.dump());
            for (auto const & h : result.hypotheses) {
                logger->info("    hypothesis: text=\"{}\" conf={}", h.text, h.confidence);
            }
            gotResult = true;
        }
    });

    // 订阅错误回调
    recognizer->subscribe(ErrorCallback{
        [&](Vsdk::details::StatusError<Vsdk::Asr::RecognizerErrorCode> const & err) {
            logger->warn("  [错误] {} : {}", err.codeString, err.message);
        }
    });

    // ─────────────────────────────────────────────────────────
    // 5. 设置模型
    // ─────────────────────────────────────────────────────────
    logger->info("[5] 设置模型 \"{}\"", modelName);
    recognizer->setModel(modelName);
    logger->info("  ✓ setModel 完成");

    // ─────────────────────────────────────────────────────────
    // 6. 用真实 Audio::Buffer 处理静音音频（1 秒，16kHz mono）
    // ─────────────────────────────────────────────────────────
    logger->info("[6] 构造 Audio::Buffer 并通过 ConsumerModule 接口处理数据 (16kHz, mono, 1s)");

    Vsdk::Audio::Buffer buf(16000, 1);
    logger->info("  ✓ Buffer 创建: sampleRate={} channelCount={}",
                 buf.sampleRate(), buf.channelCount());

    std::vector<int16_t> silence(16000, 0);   // 1 秒静音
    buf.append(silence);
    logger->info("  ✓ append 后 size={} samples ({} ms)",
                 buf.size(), buf.size() * 1000 / buf.sampleRate());

    // process() 在 Recognizer 中声明为 private，
    // 但通过 ConsumerModule 基类指针可以合法调用
    auto* consumer = static_cast<Vsdk::Audio::ConsumerModule*>(recognizer.get());
    consumer->process(buf, true);   // last=true 表示这是最后一块

    // 短暂等待异步回调（最多 1 秒）
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline && eventCount.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    logger->info("  upTime = {} ms", recognizer->upTime());

    // 打印收到的事件
    {
        std::lock_guard<std::mutex> lock(eventMu);
        logger->info("[7] 共收到 {} 个事件:", eventLog.size());
        for (auto const & e : eventLog) {
            logger->info("    事件: {}", e);
        }
    }

    logger->info("  gotResult = {}", gotResult.load());

    logger->info("================================================");
    logger->info("  ASR 测试完成");
    logger->info("================================================");
}

