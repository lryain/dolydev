#include "autocharge/config_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <exception>
#include <sstream>
#include <string>

namespace doly::autocharge {

namespace {

template <typename T>
void assignIfPresent(const YAML::Node& node, const char* key, T& target) {
    if (node && node[key]) {
        target = node[key].as<T>();
    }
}

int parseLcdSide(const YAML::Node& node, int current_value) {
    if (!node) {
        return current_value;
    }
    const std::string side = node.as<std::string>();
    return side == "left" ? 0 : 1;
}

cv::Scalar parseColor(const YAML::Node& node, const cv::Scalar& fallback) {
    if (!node) {
        return fallback;
    }

    if (node.IsSequence() && node.size() == 3) {
        const int red = node[0].as<int>();
        const int green = node[1].as<int>();
        const int blue = node[2].as<int>();
        return cv::Scalar(blue, green, red);
    }

    if (node.IsScalar()) {
        std::string value = node.as<std::string>();
        if (!value.empty() && value.front() == '#') {
            value.erase(value.begin());
        }
        if (value.size() == 6) {
            for (char ch : value) {
                if (!std::isxdigit(static_cast<unsigned char>(ch))) {
                    return fallback;
                }
            }
            const int red = std::stoi(value.substr(0, 2), nullptr, 16);
            const int green = std::stoi(value.substr(2, 2), nullptr, 16);
            const int blue = std::stoi(value.substr(4, 2), nullptr, 16);
            return cv::Scalar(blue, green, red);
        }
    }

    return fallback;
}

void loadTextConfig(const YAML::Node& node, DebugTextConfig& config) {
    assignIfPresent(node, "enabled", config.enabled);
    assignIfPresent(node, "x", config.x);
    assignIfPresent(node, "y", config.y);
    assignIfPresent(node, "scale", config.scale);
    assignIfPresent(node, "thickness", config.thickness);
    assignIfPresent(node, "outline", config.outline);
    if (node && node["color"]) {
        config.color = parseColor(node["color"], config.color);
    }
}

void loadLineConfig(const YAML::Node& node, DebugLineConfig& config) {
    assignIfPresent(node, "enabled", config.enabled);
    assignIfPresent(node, "thickness", config.thickness);
    assignIfPresent(node, "radius", config.radius);
    if (node && node["color"]) {
        config.color = parseColor(node["color"], config.color);
    }
}

}  // namespace

bool loadServiceConfig(const std::string& path, ServiceConfig& config, std::string* error_message) {
    try {
        const YAML::Node root = YAML::LoadFile(path);
        const YAML::Node autocharge = root["autocharge"] ? root["autocharge"] : root;

        const YAML::Node marker = autocharge["marker"];
        assignIfPresent(marker, "id", config.marker_id);
        assignIfPresent(marker, "size_m", config.marker_size_m);
        assignIfPresent(marker, "max_upscale", config.marker_max_upscale);

        const YAML::Node camera = autocharge["camera"];
        assignIfPresent(camera, "width", config.video_width);
        assignIfPresent(camera, "height", config.video_height);
        assignIfPresent(camera, "framerate", config.framerate);
        assignIfPresent(camera, "frame_timeout_ms", config.frame_timeout_ms);

        const YAML::Node display = autocharge["display"];
        assignIfPresent(display, "enable_lcd", config.enable_lcd);
        assignIfPresent(display, "dump_dir", config.dump_dir);
        if (display && display["lcd_side"]) {
            config.lcd_side = parseLcdSide(display["lcd_side"], config.lcd_side);
        }
        assignIfPresent(display, "brightness", config.debug_view.lcd_brightness);
        assignIfPresent(display, "lcd_width", config.lcd_width);
        assignIfPresent(display, "lcd_height", config.lcd_height);
        const YAML::Node debug_view = display["debug_view"];
        assignIfPresent(debug_view, "preview_enabled", config.debug_view.preview_enabled);
        assignIfPresent(debug_view, "canvas_size", config.debug_view.canvas_size);
        assignIfPresent(debug_view, "preview_height", config.debug_view.preview_height);
        assignIfPresent(debug_view, "panel_top", config.debug_view.panel_top);
        if (debug_view && debug_view["background_color"]) {
            config.debug_view.background_color = parseColor(debug_view["background_color"], config.debug_view.background_color);
        }
        if (debug_view && debug_view["panel_color"]) {
            config.debug_view.panel_color = parseColor(debug_view["panel_color"], config.debug_view.panel_color);
        }
        if (debug_view && debug_view["header_color"]) {
            config.debug_view.header_color = parseColor(debug_view["header_color"], config.debug_view.header_color);
        }
        loadLineConfig(debug_view["marker_box"], config.debug_view.marker_box);
        loadLineConfig(debug_view["marker_center"], config.debug_view.marker_center);
        loadLineConfig(debug_view["center_line"], config.debug_view.center_line);
        loadTextConfig(debug_view["phase"], config.debug_view.phase);
        loadTextConfig(debug_view["action"], config.debug_view.action);
        loadTextConfig(debug_view["metrics"], config.debug_view.metrics);
        loadTextConfig(debug_view["power"], config.debug_view.power);
        loadTextConfig(debug_view["tof"], config.debug_view.tof);
        loadTextConfig(debug_view["reason"], config.debug_view.reason);

        const YAML::Node drive = autocharge["drive"];
        assignIfPresent(drive, "enable_drive", config.enable_drive);
        assignIfPresent(drive, "search_rotate_speed", config.motion.search_rotate_speed);
        assignIfPresent(drive, "align_rotate_speed", config.motion.align_rotate_speed);
        assignIfPresent(drive, "forward_speed", config.motion.forward_speed);
        assignIfPresent(drive, "reverse_speed", config.motion.reverse_speed);
        assignIfPresent(drive, "rotate_compensation_scale", config.motion.rotate_compensation_scale);
        assignIfPresent(drive, "rotate_compensation_offset_deg", config.motion.rotate_compensation_offset_deg);

        const YAML::Node docking = autocharge["docking"];
        assignIfPresent(docking, "max_frames", config.max_frames);
        assignIfPresent(docking, "align_threshold", config.planner.align_threshold);
        assignIfPresent(docking, "near_align_threshold", config.planner.near_align_threshold);
        assignIfPresent(docking, "rotate_gain_deg", config.planner.rotate_gain_deg);
        assignIfPresent(docking, "near_rotate_gain_deg", config.planner.near_rotate_gain_deg);
        assignIfPresent(docking, "max_rotate_deg", config.planner.max_rotate_deg);
        assignIfPresent(docking, "near_max_rotate_deg", config.planner.near_max_rotate_deg);
        assignIfPresent(docking, "search_rotate_deg", config.planner.search_rotate_deg);
        assignIfPresent(docking, "final_dock_distance_m", config.planner.final_dock_distance_m);
        assignIfPresent(docking, "verify_distance_m", config.planner.verify_distance_m);
        assignIfPresent(docking, "close_area_ratio", config.planner.close_area_ratio);
        assignIfPresent(docking, "verify_area_ratio", config.planner.verify_area_ratio);
        assignIfPresent(docking, "min_forward_step_mm", config.planner.min_forward_step_mm);
        assignIfPresent(docking, "max_forward_step_mm", config.planner.max_forward_step_mm);
        assignIfPresent(docking, "approach_gain", config.planner.approach_gain);
        assignIfPresent(docking, "tof_align_trigger_distance_m", config.planner.tof_align_trigger_distance_m);
        assignIfPresent(docking, "tof_balance_rotate_gain_deg", config.planner.tof_balance_rotate_gain_deg);
        assignIfPresent(docking, "tof_max_rotate_deg", config.planner.tof_max_rotate_deg);
        assignIfPresent(docking, "tof_balance_tolerance_mm", config.planner.tof_balance_tolerance_mm);
        assignIfPresent(docking, "tof_stable_cycles", config.planner.tof_stable_cycles);
        assignIfPresent(docking, "align_stable_cycles", config.planner.align_stable_cycles);
        assignIfPresent(docking, "approach_stable_cycles", config.planner.approach_stable_cycles);
        assignIfPresent(docking, "reacquire_rotate_deg", config.planner.reacquire_rotate_deg);
        assignIfPresent(docking, "reacquire_max_cycles", config.planner.reacquire_max_cycles);
        assignIfPresent(docking, "rotate_to_reverse_deg", config.planner.rotate_to_reverse_deg);
        assignIfPresent(docking, "reverse_step_mm", config.planner.reverse_step_mm);
        assignIfPresent(docking, "reverse_contact_distance_mm", config.planner.reverse_contact_distance_mm);
        assignIfPresent(docking, "reverse_verify_cycles", config.planner.reverse_verify_cycles);
        assignIfPresent(docking, "retry_escape_distance_mm", config.planner.retry_escape_distance_mm);
        assignIfPresent(docking, "max_retry_count", config.planner.max_retry_count);

        const YAML::Node charging = autocharge["charging"];
        assignIfPresent(charging, "detect_current_a", config.planner.charge_detect_current_a);
        assignIfPresent(charging, "detect_voltage_v", config.planner.charge_detect_voltage_v);
        assignIfPresent(charging, "detect_max_current_a", config.planner.charge_detect_max_current_a);
        assignIfPresent(charging, "confirm_cycles", config.planner.charge_detect_confirm_cycles);

        const YAML::Node sensors = autocharge["sensors"];
        assignIfPresent(sensors, "shared_state_timeout_ms", config.planner.shared_state_timeout_ms);
        const YAML::Node tof_cpp = sensors["tof_cpp"];
        assignIfPresent(tof_cpp, "enabled", config.sensors.enable_cpp_tof);
        assignIfPresent(tof_cpp, "i2c_bus", config.sensors.tof_i2c_bus);
        assignIfPresent(tof_cpp, "left_address", config.sensors.tof_left_address);
        assignIfPresent(tof_cpp, "right_address", config.sensors.tof_right_address);
        assignIfPresent(tof_cpp, "continuous_period_ms", config.sensors.tof_continuous_period_ms);
        assignIfPresent(tof_cpp, "read_timeout_ms", config.sensors.tof_read_timeout_ms);
        assignIfPresent(tof_cpp, "left_offset_mm", config.sensors.tof_left_offset_mm);
        assignIfPresent(tof_cpp, "right_offset_mm", config.sensors.tof_right_offset_mm);
        return true;
    } catch (const std::exception& exception) {
        if (error_message != nullptr) {
            *error_message = exception.what();
        }
        return false;
    }
}

}  // namespace doly::autocharge