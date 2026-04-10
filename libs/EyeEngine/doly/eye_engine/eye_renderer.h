#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <map>

#include "doly/eye_engine/parameter_bus.h"
#include "doly/eye_engine/stylepack_loader.h"
#include "doly/eye_engine/texture_loader.h"

namespace doly::eye_engine {

class FrameBuffer;

struct RenderParams {
    std::string style{"classic"};
    std::string iris_mode{"solid"};
    std::string iris_pattern{"digital_spokes"};
    std::uint8_t iris_color_r{60};
    std::uint8_t iris_color_g{140};
    std::uint8_t iris_color_b{220};
    double iris_scale{0.85};
    
    // P2-I2: 虹膜纹理扩展参数
    std::uint8_t iris_inner_color_r{80};    // 内圈渐变颜色
    std::uint8_t iris_inner_color_g{160};
    std::uint8_t iris_inner_color_b{240};
    std::uint8_t iris_outer_color_r{40};    // 外圈渐变颜色
    std::uint8_t iris_outer_color_g{100};
    std::uint8_t iris_outer_color_b{180};
    double iris_ring_contrast{1.0};         // 环状对比度 (0.5-2.0)
    double iris_noise_scale{0.2};           // 噪声缩放 (0.0-1.0)
    double iris_noise_intensity{0.3};       // 噪声强度 (0.0-1.0)
    int iris_noise_seed{42};                // 噪声种子
    bool iris_gradient_enabled{false};      // 是否启用渐变
    
    double eye_scale{0.85};
    std::uint8_t sclera_color_r{252};
    std::uint8_t sclera_color_g{252};
    std::uint8_t sclera_color_b{255};
    double eyelid_close{0.0};
    double eyelid_upper_close{0.0};
    double eyelid_lower_close{0.0};
    double eyelid_angle{0.0};
    std::string eyelid_profile{"round"};      // 眼皮轮廓: round/flat/curved/anime
    double eyelid_thickness{0.15};            // 眼皮厚度 (0.0-0.5)
    double eyelid_curvature{0.5};             // 眼皮曲率 (0.0=平直, 1.0=圆弧)
    double eyelid_droop{0.0};                 // 眼皮下垂度 (-0.5 到 0.5, 正=下垂/悲伤, 负=上扬/警觉)
    double upper_shape_bend{0.0};             // 上眼皮形状弯曲度 (-0.5凹 到 0.5凸)
    double lower_shape_bend{0.0};             // 下眼皮形状弯曲度 (-0.5凹 到 0.5凸)
    double eyelid_opacity{1.0};
    std::uint8_t eyelid_color_r{0};
    std::uint8_t eyelid_color_g{0};
    std::uint8_t eyelid_color_b{0};
    bool eyelash_enabled{false};             // 睫毛开关
    bool eyelash_use_color{false};           // 是否使用自定义睫毛颜色
    double eyelash_length{0.12};             // 睫毛长度 (相对半高 0.0-0.4)
    double eyelash_thickness{0.08};          // 睫毛粗细 (相对半高 0.02-0.3)
    double eyelash_density{0.6};             // 睫毛密度 (0.0-1.0)
    double eyelash_curl{0.25};               // 睫毛弯曲/外扩 (-1.0 到 1.0)
    std::uint8_t eyelash_color_r{0};
    std::uint8_t eyelash_color_g{0};
    std::uint8_t eyelash_color_b{0};
    double pupil_size{0.85};
    double pupil_size_min{0.4};
    double pupil_size_max{0.9};
    std::string pupil_shape{"round"};
    double pupil_curve{1.0};
    double pupil_sharpness{2.0};  // 瞳孔尖锐度 (1.0=圆润, 2.0=标准椭圆, 3.0+=尖锐猫眼)
    double pupil_light_sensitivity{0.5};  // 光照敏感度 (0.0=不反应, 1.0=完全反应)
    std::uint8_t pupil_color_r{0};
    std::uint8_t pupil_color_g{0};
    std::uint8_t pupil_color_b{0};
    double look_x{0.0};
    double look_y{0.0};
    double look_stiffness{1.5};
    double look_damping{0.2};
    std::string highlight_style{"default"};     // 高光风格: default/soft/spark/glass/anime
    std::string highlight_shape{"circle"};      // 高光形状: circle/oval/star/bar/triangle/crescent/double
    double highlight_intensity{0.1};            // 高光强度 (0.0-1.0)
    double highlight_softness{0.4};             // 边缘柔和度 (0.0=锐利, 1.0=柔和)
    double highlight_hardness{0.0};             // P2-H1: 高光硬度 (0.0=正常, 1.0=强制硬边缘且不透明)
    double highlight_soft_exponent{3.5};        // 🆕 P2-H2: 高光边缘锐化度 (越大越锐利, 默认 3.5)
    double highlight_alpha{0.85};               // 透明度 (0.0-1.0)
    double highlight_offset_x{0.0};             // X偏移 (-1.0 到 1.0, 相对于虹膜半径)
    double highlight_offset_y{0.0};             // Y偏移 (-1.0 到 1.0, 相对于虹膜半径)
    double highlight_size{1.0};                 // P2-H1: 高光大小缩放 (0.5-2.0)
    double highlight_rotation{0.0};             // P2-H1: 高光旋转角度 (度)
    double highlight_secondary_intensity{0.0};  // P2-H1: 次高光强度 (0.0-0.5)
    double highlight_secondary_offset_x{0.3};   // P2-H1: 次高光X偏移
    double highlight_secondary_offset_y{0.3};   // P2-H1: 次高光Y偏移
    double highlight_double_secondary_scale{1.0};  // 🆕 P2-H3: 双圆形高光的第二个小高光相对尺度 (0.2-1.0)
    double highlight_crescent_phase{0.2};       // 🆕 P2-H3: 月牙形高光的月相 (0.0=初一, 0.5=十五, 1.0=二十九)
    std::uint8_t highlight_color_r{255};
    std::uint8_t highlight_color_g{255};
    std::uint8_t highlight_color_b{255};
    std::string decor_payload;
    // Performance tuning for decor ROI fallback
    double decor_target_frame_ms{16.67};
    double decor_ema_alpha_initial{0.4};
    double decor_ema_alpha{0.15};
    double decor_log_throttle_ms{2000.0};
    int decor_status_log_period_frames{120};
};

/**
 * Procedural eye renderer supporting multiple styles (classic, cat, demon, mech).
 * Renders to a 240x240 RGB565 buffer based on ProceduralProfile parameters.
 */
class EyeRenderer {
public:
    explicit EyeRenderer(ParameterBus& param_bus);

    // Performance metric accessors
    double getFrameTimeAvgMs() const { return frame_time_avg_ms_; }
    bool isSkipDecor() const { return skip_decor_; }

    /**
     * Set decoration layers from StylePack.
     */
    void setDecorLayers(const std::vector<DecorLayerSpec>& layers, const std::string& stylepack_root);

    /**
     * Set the assets root path for loading StylePacks.
     */
    void setAssetsRoot(const std::filesystem::path& assets_root);

    /**
     * Renders the current procedural profile to the provided buffer.
     * Buffer must be at least 240*240*2 bytes (RGB565).
     * Returns true on success, false on parameter parse error.
     */
    bool render(std::uint8_t* buffer, std::size_t buffer_size);

    /**
     * Renders to a FrameBuffer object.
     */
    bool render(FrameBuffer& fb);

private:
    void ensureParametersUpToDate(const ProceduralProfileSnapshot& snapshot);
    void applyStyleDefaults(RenderParams& params) const;
    void computeGeometry();

    // Internal rendering methods for each layer
    void renderBackground(std::uint8_t* buffer) const;
    void renderIris(std::uint8_t* buffer) const;
    void renderPupil(std::uint8_t* buffer) const;
    void renderEyelid(std::uint8_t* buffer) const;
    void renderHighlight(std::uint8_t* buffer) const;
    void renderDecor(std::uint8_t* buffer) const;

    // Utility: RGB888 to RGB565
    static std::uint16_t rgb888To565(std::uint8_t r, std::uint8_t g, std::uint8_t b);

    int irisCenterX() const;
    int irisCenterY() const;
    int irisRadius() const;
    int pupilRadius() const;

    ParameterBus& param_bus_;
    RenderParams params_{};
    std::uint64_t cached_version_{0};

    // Performance tracking for ROI / decor fallback
    std::uint64_t frame_count_{0};
    double frame_time_avg_ms_{0.0}; // exponential moving average of frame time in ms
    bool skip_decor_{false};

    // Logging / throttling helpers
    std::chrono::steady_clock::time_point last_toggle_log_ts_ = std::chrono::steady_clock::time_point{};
    int frames_since_status_log_{0};

    // Decoration layers
    std::vector<DecorLayerSpec> decor_layers_;
    std::string stylepack_root_path_;
    // Cache loaded decor textures keyed by layer texture path
    std::map<std::string, Texture> decor_textures_;
    std::unique_ptr<TextureLoader> texture_loader_;
    std::filesystem::path assets_root_;
    std::string current_stylepack_id_;

    struct DerivedGeometry {
        int center_x{120};
        int center_y{120};
        double eye_radius{60.0};
        int iris_radius{80};
        int pupil_radius{48};
    };

    DerivedGeometry geometry_{};

    // 🆕 Visual curve parameters for crescent_phase rendering (P2-H3: 可配置月牙映射)
    struct VisualCurveParams {
        double crescent_gamma{0.3};      // Exponential power for phase -> visibility mapping
        double crescent_scale{0.9};      // Scale factor for visibility curve
        double crescent_offset{0.02};    // Minimum visibility offset
    };
    VisualCurveParams visual_curves_;

    // Load visual curve configuration from visual_curves.json
    void loadVisualCurves();
};

}  // namespace doly::eye_engine