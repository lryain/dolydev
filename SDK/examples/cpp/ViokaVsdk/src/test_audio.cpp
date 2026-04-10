/**
 * @file test_audio.cpp
 * @brief Audio (音频处理) 真实测试 - 使用 Vsdk::Audio::Buffer
 *
 * 使用真实 API 调用验证缓冲区创建、数据追加、保存文件等功能。
 */

#include <vsdk/audio/Buffer.hpp>

#include <spdlog/spdlog.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

extern std::shared_ptr<spdlog::logger> get_logger();

/**
 * @brief 生成正弦波 PCM 数据
 * @param hz        频率 (Hz)
 * @param sampleRate 采样率
 * @param durationMs 持续时间 (ms)
 */
static std::vector<int16_t> make_sine(int hz, int sampleRate, int durationMs)
{
    int samples = sampleRate * durationMs / 1000;
    std::vector<int16_t> out(samples);
    for (int i = 0; i < samples; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        out[i] = static_cast<int16_t>(16000.0 * std::sin(2.0 * M_PI * hz * t));
    }
    return out;
}

void test_audio()
{
    auto logger = get_logger();

    logger->info("================================================");
    logger->info("  Audio 真实测试 - Vsdk::Audio::Buffer");
    logger->info("================================================");

    // ─────────────────────────────────────────────────────────
    // 1. 默认构造 (16kHz, mono)
    // ─────────────────────────────────────────────────────────
    logger->info("[1] 默认构造 Buffer()");
    {
        Vsdk::Audio::Buffer buf;
        assert(buf.sampleRate()   == 16000);
        assert(buf.channelCount() == 1);
        assert(buf.empty());
        assert(buf.size()         == 0);
        logger->info("  ✓ sampleRate={} channelCount={} empty={} size={}",
                     buf.sampleRate(), buf.channelCount(), buf.empty(), buf.size());
    }

    // ─────────────────────────────────────────────────────────
    // 2. 带参构造 + append vector
    // ─────────────────────────────────────────────────────────
    logger->info("[2] Buffer(44100, 2) + append(vector)");
    {
        Vsdk::Audio::Buffer buf(44100, 2);
        assert(buf.sampleRate()   == 44100);
        assert(buf.channelCount() == 2);
        assert(buf.empty());

        std::vector<int16_t> samples(4410, 1000);  // 0.1 s silence
        buf.append(samples);
        assert(buf.size() == 4410);
        assert(!buf.empty());
        logger->info("  ✓ sampleRate={} channelCount={} size={} (expected 4410)",
                     buf.sampleRate(), buf.channelCount(), buf.size());
    }

    // ─────────────────────────────────────────────────────────
    // 3. 从 vector<int16_t> 构造
    // ─────────────────────────────────────────────────────────
    logger->info("[3] Buffer(vector, sampleRate, channelCount)");
    {
        std::vector<int16_t> data(8000, 42);
        Vsdk::Audio::Buffer buf(data, 8000, 1);
        assert(buf.size()       == 8000);
        assert(buf.sampleRate() == 8000);
        assert(buf.data()[0]    == 42);
        assert(buf.rawData()[7999] == 42);
        logger->info("  ✓ size={} data[0]={} rawData[7999]={}",
                     buf.size(), buf.data()[0], buf.rawData()[7999]);
    }

    // ─────────────────────────────────────────────────────────
    // 4. float → int16 有损构造
    // ─────────────────────────────────────────────────────────
    logger->info("[4] Buffer(vector<float>, sampleRate, channelCount)");
    {
        std::vector<float> fdata(1600, 0.5f);
        Vsdk::Audio::Buffer buf(fdata, 16000, 1);
        assert(buf.size() == 1600);
        // float 0.5 → int16 应接近 16383
        int16_t v = buf.data()[0];
        logger->info("  ✓ size={} float(0.5)->int16={}", buf.size(), v);
        assert(v > 10000);  // 合理范围检查
    }

    // ─────────────────────────────────────────────────────────
    // 5. append 指针形式
    // ─────────────────────────────────────────────────────────
    logger->info("[5] append(ptr, size)");
    {
        Vsdk::Audio::Buffer buf(16000, 1);
        std::vector<int16_t> raw(500, -1);
        buf.append(raw.data(), raw.size());
        assert(buf.size() == 500);
        assert(buf.rawData()[0] == -1);
        logger->info("  ✓ size={} rawData[0]={}", buf.size(), buf.rawData()[0]);
    }

    // ─────────────────────────────────────────────────────────
    // 6. clear() 重置
    // ─────────────────────────────────────────────────────────
    logger->info("[6] clear()");
    {
        Vsdk::Audio::Buffer buf(16000, 1);
        buf.append(std::vector<int16_t>(1000, 0));
        assert(buf.size() == 1000);
        buf.clear();
        assert(buf.size()  == 0);
        assert(buf.empty() == true);
        logger->info("  ✓ 清除后 size={} empty={}", buf.size(), buf.empty());
    }

    // ─────────────────────────────────────────────────────────
    // 7. setMaxSize 缓冲区大小限制
    // ─────────────────────────────────────────────────────────
    logger->info("[7] setMaxSize (buffer size limit)");
    {
        Vsdk::Audio::Buffer buf(16000, 1);
        buf.setMaxSize(1000);
        assert(buf.maxSize() == 1000);

        // append 不超过 maxSize 的数据，正常工作
        buf.append(std::vector<int16_t>(500, 7));
        assert(buf.size() == 500);
        buf.append(std::vector<int16_t>(400, 7));
        assert(buf.size() == 900);
        logger->info("  maxSize={}  size after 900 append = {} (≤ maxSize)",
                     buf.maxSize(), buf.size());

        // append 超过 maxSize 会抛出异常
        bool caughtException = false;
        try {
            buf.append(std::vector<int16_t>(2000, 0));  // 超过 maxSize
        } catch (std::exception const & e) {
            caughtException = true;
            logger->info("  ✓ 超出 maxSize 正确抛出异常: {}", e.what());
        }
        assert(caughtException);
        logger->info("  ✓ setMaxSize 缓冲区限制工作正常");
    }

    // ─────────────────────────────────────────────────────────
    // 8. saveToFile 保存并验证文件大小
    // ─────────────────────────────────────────────────────────
    logger->info("[8] saveToFile → /tmp/vsdk_audio_test.pcm");
    {
        auto sineData = make_sine(440, 16000, 500);  // 440 Hz, 0.5 s
        Vsdk::Audio::Buffer buf(sineData, 16000, 1);
        logger->info("  正弦波: 440 Hz, 0.5s, {} 样本", buf.size());

        std::string outPath = "/tmp/vsdk_audio_test.pcm";
        buf.saveToFile(outPath);

        // 验证文件存在且大小正确 (size * 2 bytes/sample)
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(outPath, ec);
        assert(!ec);
        assert(fileSize == buf.size() * 2);
        logger->info("  ✓ 文件已保存: {} ({} bytes, 期望 {} bytes)",
                     outPath, fileSize, buf.size() * 2);
    }

    // ─────────────────────────────────────────────────────────
    // 9. takeData 移动语义
    // ─────────────────────────────────────────────────────────
    logger->info("[9] takeData()");
    {
        Vsdk::Audio::Buffer buf(16000, 1);
        buf.append(std::vector<int16_t>(320, 99));
        assert(buf.size() == 320);

        auto moved = std::move(buf).takeData();
        assert(moved.size() == 320);
        assert(moved[0] == 99);
        logger->info("  ✓ takeData 返回 {} 个样本, [0]={}", moved.size(), moved[0]);
    }

    logger->info("================================================");
    logger->info("  Audio 测试全部通过");
    logger->info("================================================");
}
