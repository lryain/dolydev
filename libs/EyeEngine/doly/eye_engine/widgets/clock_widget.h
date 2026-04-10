#pragma once

#include "doly/eye_engine/widgets/widget_interface.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <chrono>

#include <nlohmann/json.hpp>
#include "doly/eye_engine/widgets/widget_audio_player.h"
#include "doly/eye_engine/widgets/widget_tts_helper.h"

namespace zmq {
class context_t;
class socket_t;
}

namespace doly {
namespace eye_engine {

class ClockWidget : public WidgetBase {
public:
    enum class ClockLayout {
        kSingle,
        kSplit
    };

    enum class HourFormat {
        k24Hour,
        k12Hour
    };

    enum class SplitRole {
        kHour,
        kMinute,
        kSecond
    };

    struct RgbColor {
        uint8_t r = 255;
        uint8_t g = 255;
        uint8_t b = 255;
    };

    struct AudioConfig {
        bool enabled = false;
        bool tick_sound_en = false;
        std::string tick_sound;
        std::string cmd_endpoint = "ipc:///tmp/doly_audio_player_cmd.sock";
        int tick_priority = 20;
        int req_timeout_ms = 150;
    };

    // 整点报时配置
    struct ChimeConfig {
        bool enabled = false;                       // 是否开启整点报时
        std::string language = "inherit";          // 播报语言：zh/zh-cn/en/inherit
        std::string text_template = "现在时间 {HH}:{MM}";                  // 中文整点播报文案模板
        std::string announce_template = "现在时间 {HH} 点 {MM} 分";       // 中文立即播报文案
        std::string text_template_en = "The time is {HH}:{MM}";          // 英文整点播报
        std::string announce_template_en = "The time is {HH} {MM}";      // 英文立即播报
        std::string text_template_24h_zh = "现在是{period}{HH}点{MM}分";
        std::string text_template_12h_zh = "现在是{period}{HH}点{MM}分";
        std::string text_template_24h_en = "The time is {HH} {MM}";
        std::string text_template_12h_en = "The time is {HH} {MM} in the {period}";
        std::string announce_template_24h_zh = "现在是{period}{HH}点{MM}分";
        std::string announce_template_12h_zh = "现在是{period}{HH}点{MM}分";
        std::string announce_template_24h_en = "The time is {HH} {MM}";
        std::string announce_template_12h_en = "The time is {HH} {MM} in the {period}";
        std::string tts_engine = "espeak-ng";      // edge-tts | espeak-ng
    std::string espeak_command = "espeak-ng -v zh '{text}' >/dev/null 2>&1";  // espeak-ng 中文模板
    std::string espeak_command_en = "espeak-ng -v en '{text}' >/dev/null 2>&1";  // espeak-ng 英文模板
        std::string edge_tts_transport = "zmq";    // zmq | cmd
        std::string edge_tts_endpoint = "ipc:///tmp/doly_tts_req.sock";
        std::string edge_tts_client_bin = "python3 /home/pi/dev/nora-xiaozhi-dev/libs/tts/tts-server/edge_tts_client.py";
    std::string edge_tts_voice = "zh-CN-YunxiaNeural";
    std::string edge_tts_voice_en = "en-US-JennyNeural";
        std::string edge_tts_pitch = "+0Hz";
        std::string edge_tts_rate = "+0%";
        std::string edge_tts_volume = "+0%";
        std::string edge_tts_format = "wav";
        bool edge_tts_play = true;
        std::string edge_tts_play_mode = "audio_player";
        float edge_tts_ap_volume = 1.0f;
        std::string edge_tts_alias = "clock_chime";
        bool edge_tts_ducking = true;
        int edge_tts_timeout_ms = 10000;
        int edge_tts_retries = 1;
    };

    // 查询接口配置
    struct ApiConfig {
        bool enabled = false;                                 // 是否启用查询接口
        std::string endpoint = "ipc:///tmp/doly_clock_api.sock";  // ZMQ REP 端点
    };

    struct ClockConfig {
        ClockLayout layout = ClockLayout::kSplit;
        HourFormat hour_format = HourFormat::k24Hour;
        SplitRole left_role = SplitRole::kMinute;
        SplitRole right_role = SplitRole::kHour;
        // 注意：show_seconds 不从配置文件读取，而是根据 left_role/right_role 自动推导
        // 如果 left_role 或 right_role 为 kSecond，则自动设置为 true
        bool show_seconds = false;
        bool colon_blink = false;
        RgbColor digit_color{255, 255, 255};
        RgbColor background_color{0, 0, 0};
        // 🆕 字体缩放百分比（基于可适配尺寸再按此比例缩放，默认 100%）
        // 建议范围 50-150，可适配小屏且避免溢出
        int digit_scale_percent = 100;
    };

    explicit ClockWidget();
    ~ClockWidget() override;

    void onShow(const nlohmann::json& config) override;
    void onHide() override;
    void update(double delta_time_ms) override;
    void prepareFrame(const WidgetContext& ctx) override;
    void render(FrameBuffer& buffer, const WidgetContext& ctx) override;
    ROI getUpdateROI() const override;
    bool needsRedraw() const override;
    bool updateConfig(const std::string& config_json) override;
    std::string getConfig() const override;
    void reset() override;

    // 测试辅助：注入/清理模拟时间
    void setMockTime(const std::tm& tm);
    void clearMockTime();

private:
    // 选择语言时使用的模板
    std::string effectiveLanguage() const;
    bool isLanguageEnglish(const std::optional<std::string>& language_override = std::nullopt) const;
    std::string resolveLanguage(const std::optional<std::string>& language_override) const;
    std::string selectChimeTemplate(bool announce, const std::optional<std::string>& language_override = std::nullopt) const;
    std::string periodLabel(int hour, const std::optional<std::string>& language_override) const;
    WidgetTtsConfig buildChimeTtsConfig(const std::optional<std::string>& language_override = std::nullopt) const;
    void applyConfig(const nlohmann::json& cfg);
    void applyAudioConfig(const nlohmann::json& cfg);
    void updateTime();
    void refreshUpdateInterval();
    std::array<int, 6> computeDigits(const std::tm& tm) const;
    std::pair<int, int> digitsForRole(SplitRole role) const;
    bool updateDirtyState(const std::array<int, 6>& digits);
    void markAllSidesDirty();
    void renderSplit(FrameBuffer& buffer, const WidgetContext& ctx, uint16_t fg_color, uint16_t bg_color) const;
    void renderSingle(FrameBuffer& buffer, const WidgetContext& ctx, uint16_t fg_color, uint16_t bg_color) const;
    void drawDigit(uint16_t* buffer, int width, int height, int digit, int x, int y, float scale, uint16_t color) const;
    void drawColon(uint16_t* buffer, int width, int height, int x, int y, float scale, uint16_t color) const;

    // 整点报时
    void checkChime();
    void triggerChime(int hour, int minute, const std::string& text_template,
                      const std::optional<std::string>& language_override = std::nullopt);
    std::string formatChimeText(int hour, int minute, const std::string& text_template,
                                const std::optional<std::string>& language_override) const;

    // 查询接口
    void ensureApiServer();
    void stopApiServer();
    void stopApiServerLocked();
    void apiServeLoop();

    static std::array<int, 2> indicesForRole(SplitRole role);
    static const char* layoutToString(ClockLayout layout);
    static const char* roleToString(SplitRole role);

private:
    ClockConfig config_{};
    AudioConfig audio_config_{};
    ChimeConfig chime_config_{};
    ApiConfig api_config_{};
    std::string global_language_ = "zh";

    std::tm current_time_{};
    std::tm mock_time_{};
    bool use_mock_time_ = false;
    std::chrono::steady_clock::time_point mock_set_at_{};

    std::array<int, 6> last_digits_{};
    std::array<int, 6> active_digits_{};
    std::array<bool, 2> side_dirty_{};
    bool colon_visible_ = true;

    // 整点报时防抖：记录最近一次报时的“日内小时槽”（yday*24+hour）
    int last_chime_slot_ = -1;

    // 音频播放相关
    WidgetAudioPlayer audio_player_{"clock_widget"};

    // API 查询线程与状态
    std::unique_ptr<zmq::context_t> api_ctx_;
    std::unique_ptr<zmq::socket_t> api_rep_;
    std::thread api_thread_;
    std::atomic<bool> api_stop_{false};
    std::string api_bound_endpoint_;
    std::mutex api_mutex_;

    void playTickSound();
};

}  // namespace eye_engine
}  // namespace doly
