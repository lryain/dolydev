#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct DolyEyeState {
    const char* style;
    const char* iris_mode;
    const char* iris_pattern;
    double iris_color_r;
    double iris_color_g;
    double iris_color_b;
    double iris_scale;
    double eye_scale;
    double sclera_color_r;
    double sclera_color_g;
    double sclera_color_b;
    double eyelid_close;
    double eyelid_upper_close;
    double eyelid_lower_close;
    double eyelid_angle;
    const char* eyelid_profile;
    double eyelid_opacity;
    double eyelid_color_r;
    double eyelid_color_g;
    double eyelid_color_b;
    double pupil_size;
    double pupil_size_min;
    double pupil_size_max;
    const char* pupil_shape;
    double pupil_curve;
    double pupil_color_r;
    double pupil_color_g;
    double pupil_color_b;
    double look_x;
    double look_y;
    double look_stiffness;
    double look_damping;
    const char* highlight_style;
    const char* highlight_shape;
    double highlight_intensity;
    double highlight_softness;
    double highlight_alpha;
    double highlight_color_r;
    double highlight_color_g;
    double highlight_color_b;
    double highlight_offset_x;
    double highlight_offset_y;
    const char* decor_payload;
};

struct DolyRuntimeState {
    struct DolyEyeState left_eye;
    struct DolyEyeState right_eye;
    double backlight;
    
    // 眨眼行为配置
    int scheduler_blink_enabled;           // 是否启用自动眨眼
    double scheduler_blink_rate;           // 每分钟眨眼次数
    double scheduler_blink_randomness;     // 眨眼间隔随机度
    double blink_close_speed;              // 眨眼闭合速度
    double blink_close_percentage_left;    // 左眼眨眼幅度
    double blink_close_percentage_right;   // 右眼眨眼幅度
    
    // 注视点变化行为配置
    int scheduler_gaze_enabled;            // 是否启用自动注视点变化（眼球微动）
    double scheduler_gaze_frequency;       // 注视点变化频率（Hz）
    double scheduler_gaze_damping;         // 视线弹簧阻尼系数
    double eye_move_speed;                 // 眼球移动速度倍率
    
    double pupil_scale_speed;              // 瞳孔缩放速度
};

void* doly_eye_engine_runtime_create();
int doly_eye_engine_runtime_start(void* handle);
int doly_eye_engine_runtime_stop(void* handle);
int doly_eye_engine_runtime_update(void* handle, const struct DolyRuntimeState* state);
int doly_eye_engine_runtime_blink(void* handle, int count);
void doly_eye_engine_runtime_destroy(void* handle);

// Retrieve renderer performance metrics. Returns 1 on success, 0 on failure.
int doly_eye_engine_runtime_get_metrics(void* handle,
                                      double* left_avg_ms,
                                      int* left_skip,
                                      double* right_avg_ms,
                                      int* right_skip);

// Set stylepack decorations JSON and root path for native renderer to load textures.
int doly_eye_engine_runtime_set_stylepack_decor(void* handle, const char* stylepack_root, const char* decorations_json);

// Set maximum frames per second for the runtime. If fps <= 0, no change.
int doly_eye_engine_runtime_set_max_fps(void* handle, double fps);

// Get current maximum frames per second (returns 0 on failure).
double doly_eye_engine_runtime_get_max_fps(void* handle);

// Update Widget configuration from JSON. Returns 1 on success, 0 on failure.
// widgets_json: JSON string containing Widget configuration
// json_len: length of the JSON string (0 means disable all widgets)
int doly_eye_engine_runtime_update_widgets(void* handle, const char* widgets_json, int json_len);

// Control Timer Widget with a command. Returns 1 on success, 0 on failure.
// command: one of "start", "pause", "resume", "reset", "stop"
int doly_eye_engine_runtime_control_timer(void* handle, const char* command);

// Start circle gaze animation (fox-like eye movement). Returns 1 on success, 0 on failure.
// duration_seconds: animation duration in seconds (0.1 ~ 10.0)
// radius: circle radius in eye coordinate system (0.1 ~ 0.9)
// laps: number of circles to complete (1 ~ 10)
// clockwise: 1 for clockwise, 0 for counter-clockwise
int doly_eye_engine_runtime_start_circle_gaze(void* handle, double laps, double radius, double speed, int clockwise);

// Stop circle gaze animation. Returns 1 on success, 0 on failure.
int doly_eye_engine_runtime_stop_circle_gaze(void* handle);

// LCD Control API - Enable/disable individual LCD displays
// eye: 0 for left, 1 for right
// enabled: 1 to enable, 0 to disable
// Returns 1 on success, 0 on failure
int doly_eye_engine_runtime_set_lcd_enabled(void* handle, int eye, int enabled);

// Enable/disable both LCD displays at once
// left_enabled: 1 to enable left eye, 0 to disable
// right_enabled: 1 to enable right eye, 0 to disable
// Returns 1 on success, 0 on failure
int doly_eye_engine_runtime_set_lcd_enabled_both(void* handle, int left_enabled, int right_enabled);

// Check if LCD display is enabled for a given eye
// eye: 0 for left, 1 for right
// Returns 1 if enabled, 0 if disabled, -1 on error
int doly_eye_engine_runtime_is_lcd_enabled(void* handle, int eye);

// ============================================================
// Widget Control API (Phase 4 - ZeroMQ Integration)
// ============================================================

// Show a widget on specified slot(s)
// widget_id: Widget identifier (e.g., "clock", "timer", "image")
// slot: "left_eye", "right_eye", or "both"
// timeout_ms: Auto-hide timeout in milliseconds (0 for no timeout)
// config_json: Widget-specific configuration JSON (NULL for default)
// Returns 1 on success, 0 on failure
int doly_eye_engine_runtime_show_widget(void* handle, 
                                        const char* widget_id,
                                        const char* slot,
                                        uint32_t timeout_ms,
                                        const char* config_json);

// Hide widget from specified slot(s)
// slot: "left_eye", "right_eye", or "both"
// Returns 1 on success, 0 on failure
int doly_eye_engine_runtime_hide_widget(void* handle, const char* slot);

// Check widget status
// slot: "left_eye" or "right_eye"
// Returns: 0 = rendering (no widget), 1 = widget active, -1 = error
int doly_eye_engine_runtime_widget_status(void* handle, const char* slot);

#ifdef __cplusplus
}
#endif
