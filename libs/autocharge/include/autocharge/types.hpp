#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace doly::autocharge {

enum class DockingPhase {
    Searching,
    Aligning,
    Approaching,
    Reacquire,
    TofAlign,
    RotateToReverse,
    ReverseDock,
    RetryEscape,
    RetryRotateFront,
    FinalDock,
    VerifyCharging,
    Docked,
    Failed,
};

enum class MotionType {
    None,
    Rotate,
    Forward,
    Backward,
    Stop,
};

struct MarkerObservation {
    bool found = false;
    int marker_id = -1;
    // true if the marker id was obtained by ArUco decoding; false if inferred by printed-sign heuristic
    bool decoded = false;
    double normalized_x = 0.0;
    double area_ratio = 0.0;
    double distance_m = -1.0;
    bool pose_valid = false;
    cv::Vec3d rvec{0.0, 0.0, 0.0};
    cv::Vec3d tvec{0.0, 0.0, 0.0};
    std::vector<cv::Point2f> corners;
};

struct PowerStatus {
    bool valid = false;
    float voltage_v = 0.0f;
    float current_a = 0.0f;
    std::uint8_t percentage = 0;
    bool low_battery = false;
    bool is_charging = false;
    std::string source = "unknown";
};

struct TofStatus {
    bool valid = false;
    bool left_valid = false;
    bool right_valid = false;
    std::uint16_t left_mm = 0;
    std::uint16_t right_mm = 0;
    std::uint16_t min_distance_mm = 0;
    bool obstacle_detected = false;
    int balance_error_mm = 0;
    std::string source = "unknown";
};

struct MotionCommand {
    MotionType type = MotionType::None;
    DockingPhase phase = DockingPhase::Searching;
    double rotate_deg = 0.0;
    double forward_mm = 0.0;
    bool should_stop = false;
    bool completed = false;
    std::string reason;
};

struct CameraIntrinsics {
    cv::Mat camera_matrix;
    cv::Mat dist_coeffs;

    static CameraIntrinsics createApproximate(int width, int height);
};

struct MotionTuningConfig {
    int search_rotate_speed = 35;
    int align_rotate_speed = 28;
    int forward_speed = 30;
    int reverse_speed = 20;
    double rotate_compensation_scale = 1.0;
    double rotate_compensation_offset_deg = 0.0;
};

struct SensorServiceConfig {
    bool enable_cpp_tof = true;
    int tof_i2c_bus = 6;
    int tof_left_address = 0x29;
    int tof_right_address = 0x30;
    int tof_continuous_period_ms = 20;
    int tof_read_timeout_ms = 200;
    int tof_left_offset_mm = 14;
    int tof_right_offset_mm = 93;
};

struct DebugTextConfig {
    bool enabled = true;
    int x = 10;
    int y = 0;
    double scale = 0.6;
    int thickness = 2;
    int outline = 4;
    cv::Scalar color{255, 255, 255};
};

struct DebugLineConfig {
    bool enabled = true;
    int thickness = 3;
    int radius = 8;
    cv::Scalar color{255, 255, 255};
};

struct DebugViewConfig {
    bool preview_enabled = true;
    int canvas_size = 240;
    int preview_height = 118;
    int panel_top = 118;
    int lcd_brightness = 8;
    cv::Scalar background_color{22, 24, 28};
    cv::Scalar panel_color{18, 18, 18};
    cv::Scalar header_color{0, 80, 120};
    DebugLineConfig marker_box{true, 6, 8, cv::Scalar(0, 255, 0)};
    DebugLineConfig marker_center{true, 2, 8, cv::Scalar(0, 220, 255)};
    DebugLineConfig center_line{true, 3, 0, cv::Scalar(255, 180, 0)};
    DebugTextConfig phase{true, 10, 140, 0.68, 2, 5, cv::Scalar(255, 255, 255)};
    DebugTextConfig action{true, 10, 168, 0.75, 2, 5, cv::Scalar(0, 220, 255)};
    DebugTextConfig metrics{true, 10, 193, 0.50, 1, 4, cv::Scalar(235, 235, 235)};
    DebugTextConfig power{true, 10, 214, 0.50, 1, 4, cv::Scalar(255, 215, 120)};
    DebugTextConfig tof{true, 10, 232, 0.48, 1, 4, cv::Scalar(180, 230, 255)};
    DebugTextConfig reason{true, 10, 238, 0.34, 1, 3, cv::Scalar(220, 220, 220)};
};

struct DockingPlannerConfig {
    double align_threshold = 0.10;
    double near_align_threshold = 0.22;
    double rotate_gain_deg = 28.0;
    double near_rotate_gain_deg = 18.0;
    double max_rotate_deg = 18.0;
    double near_max_rotate_deg = 4.0;
    double search_rotate_deg = 14.0;
    double final_dock_distance_m = 0.24;
    double verify_distance_m = 0.16;
    double close_area_ratio = 0.45;
    double verify_area_ratio = 0.62;
    double min_forward_step_mm = 25.0;
    double max_forward_step_mm = 60.0;
    double approach_gain = 0.40;
    double charge_detect_current_a = 0.02;
    double charge_detect_voltage_v = 4.0;
    double charge_detect_max_current_a = 5.0;
    double tof_align_trigger_distance_m = 0.32;
    double tof_balance_rotate_gain_deg = 0.12;
    double tof_max_rotate_deg = 6.0;
    int tof_balance_tolerance_mm = 12;
    int tof_stable_cycles = 2;
    int align_stable_cycles = 2;
    int approach_stable_cycles = 2;
    double reacquire_rotate_deg = 4.0;
    int reacquire_max_cycles = 6;
    double rotate_to_reverse_deg = 180.0;
    double reverse_step_mm = 20.0;
    double reverse_contact_distance_mm = 180.0;
    int reverse_verify_cycles = 4;
    double retry_escape_distance_mm = 80.0;
    int max_retry_count = 3;
    int charge_detect_confirm_cycles = 3;
    int shared_state_timeout_ms = 1500;
};

struct ServiceConfig {
    int marker_id = 23;
    float marker_size_m = 0.12f;
    int video_width = 1280;
    int video_height = 960;
    // When marker is far/small, try upscaling the full-frame to improve ArUco recall
    int marker_max_upscale = 3; // try scales 1 (native), 2, ... up to this value
    int framerate = 15;
    // LCD output resolution (target for pushFrameToLcd)
    int lcd_width = 240;
    int lcd_height = 240;
    int lcd_side = 1;
    int max_frames = 1200;
    int frame_timeout_ms = 1200;
    bool enable_lcd = true;
    bool enable_drive = true;
    std::string dump_dir;
    MotionTuningConfig motion;
    SensorServiceConfig sensors;
    DebugViewConfig debug_view;
    DockingPlannerConfig planner;
};

const char* toString(DockingPhase phase);
const char* toString(MotionType type);

}  // namespace doly::autocharge