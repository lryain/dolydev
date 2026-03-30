#include "autocharge/autocharge_service.hpp"

#include "Helper.h"
#include "LcdControl.h"
#include "DriveControl.h"
#include "lccv.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace doly::autocharge {

namespace {

constexpr auto kDriveDispatchGrace = std::chrono::milliseconds(300);

double applyRotateCompensation(double rotate_deg, const MotionTuningConfig& motion_config) {
    if (rotate_deg == 0.0) {
        return 0.0;
    }

    const double sign = rotate_deg > 0.0 ? 1.0 : -1.0;
    return (rotate_deg * motion_config.rotate_compensation_scale)
        + sign * motion_config.rotate_compensation_offset_deg;
}

bool isMotionDispatchCommand(const MotionCommand& command) {
    return command.type == MotionType::Rotate
        || command.type == MotionType::Forward
        || command.type == MotionType::Backward;
}

}  // namespace

AutoChargeService::AutoChargeService(ServiceConfig config)
        : config_(std::move(config)),
            renderer_(config_.debug_view) {}

AutoChargeService::~AutoChargeService() {
    shutdown();
}

bool AutoChargeService::initialize() {
    if (Helper::stopDolyService() < 0) {
        std::cerr << "[AutoCharge] Failed to stop doly service" << std::endl;
        return false;
    }

    detector_ = std::make_unique<MarkerDetector>(
        config_.marker_id,
        config_.marker_size_m,
        CameraIntrinsics::createApproximate(config_.video_width, config_.video_height));
    planner_ = std::make_unique<DockingPlanner>(config_.planner);
    power_monitor_ = std::make_unique<PowerMonitor>();
    power_monitor_->initialize();
    tof_monitor_ = std::make_unique<TofMonitor>();
    tof_monitor_->initialize();

    std::cout << "[AutoCharge] marker_id=" << config_.marker_id
              << " marker_size_m=" << config_.marker_size_m
              << " video=" << config_.video_width << "x" << config_.video_height
              << " lcd=" << (config_.enable_lcd ? (config_.lcd_side == 0 ? "left" : "right") : "disabled")
              << " drive=" << (config_.enable_drive ? "enabled" : "disabled")
              << std::endl;

    return initializeCamera() && initializeDrive() && initializeLcd();
}

bool AutoChargeService::initializeCamera() {
    camera_ = std::make_unique<PiCamera>();
    camera_->options->video_width = config_.video_width;
    camera_->options->video_height = config_.video_height;
    camera_->options->framerate = config_.framerate;
    camera_->options->verbose = false;
    camera_ready_ = camera_->startVideo();
    if (!camera_ready_) {
        std::cerr << "[AutoCharge] Failed to start camera video" << std::endl;
    }
    return camera_ready_;
}

bool AutoChargeService::initializeDrive() {
    if (!config_.enable_drive) {
        return true;
    }
    const auto rc = DriveControl::init();
    drive_ready_ = (rc == 0 || rc == 1);
    if (!drive_ready_) {
        std::cerr << "[AutoCharge] DriveControl init failed rc=" << static_cast<int>(rc) << std::endl;
    }
    return drive_ready_;
}

bool AutoChargeService::initializeLcd() {
    if (!config_.enable_lcd) {
        return true;
    }
    const auto rc = LcdControl::init(LcdColorDepth::L12BIT);
    lcd_ready_ = (rc == 0 || rc == 1);
    if (lcd_ready_) {
        LcdControl::setBrightness(config_.debug_view.lcd_brightness);
        std::cout << "[AutoCharge] LcdControl init rc=" << static_cast<int>(rc) << " active=" << (LcdControl::isActive() ? "yes" : "no") << std::endl;
        // Preallocate LCD buffer to avoid reallocations per-frame
        const size_t buf_size = static_cast<size_t>(LcdControl::getBufferSize());
        lcd_buffer_.resize(buf_size);
    } else {
        std::cerr << "[AutoCharge] LcdControl init failed rc=" << static_cast<int>(rc) << " active=" << (LcdControl::isActive() ? "yes" : "no") << std::endl;
    }
    return lcd_ready_;
}

int AutoChargeService::run() {
    if (!camera_ready_ || !detector_ || !planner_ || !power_monitor_) {
        return 2;
    }

    for (int frame_index = 0; frame_index < config_.max_frames; ++frame_index) {
        cv::Mat frame;
        std::cout << "[AutoCharge] awaiting camera frame index=" << frame_index << std::endl;
        if (!camera_->getVideoFrame(frame, static_cast<unsigned int>(config_.frame_timeout_ms))) {
            std::cerr << "[AutoCharge] Camera frame timeout" << std::endl;
            continue;
        }
        std::cout << "[AutoCharge] camera frame received index=" << frame_index << " size=" << frame.cols << "x" << frame.rows << std::endl;

        MotionCommand command = executeCycle(frame);
        if (command.completed) {
            std::cout << "[AutoCharge] Docking finished: " << command.reason << std::endl;
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 3;
}

bool AutoChargeService::isDriveBusy() {
    if (!drive_ready_) {
        return false;
    }

    if (DriveControl::getState() == DriveState::RUNNING) {
        pending_drive_command_ = false;
        return true;
    }

    if (!pending_drive_command_) {
        return false;
    }

    if ((std::chrono::steady_clock::now() - pending_drive_command_since_) <= kDriveDispatchGrace) {
        return true;
    }

    pending_drive_command_ = false;
    return false;
}

bool AutoChargeService::shouldDispatchMotion(const MotionCommand& command, bool drive_busy) const {
    if (!drive_ready_) {
        return false;
    }

    switch (command.type) {
        case MotionType::Rotate:
        case MotionType::Forward:
        case MotionType::Backward:
            return !drive_busy;
        case MotionType::Stop:
            return drive_busy;
        case MotionType::None:
            return false;
    }

    return false;
}

void AutoChargeService::noteMotionDispatch(const MotionCommand& command, bool accepted) {
    if (!accepted) {
        pending_drive_command_ = false;
        return;
    }

    if (isMotionDispatchCommand(command)) {
        pending_drive_command_ = true;
        pending_drive_command_since_ = std::chrono::steady_clock::now();
        return;
    }

    pending_drive_command_ = false;
}

MotionCommand AutoChargeService::executeCycle(const cv::Mat& frame) {
    const MarkerObservation observation = detector_->detect(frame);
    const PowerStatus power_status = power_monitor_->read(config_.planner);
    const TofStatus tof_status = tof_monitor_ ? tof_monitor_->read(config_.sensors) : TofStatus{};
    const bool drive_busy = isDriveBusy();
    MotionCommand command = planner_->plan(observation, power_status, tof_status, drive_busy);
    ++frame_counter_;

    logCycleSummary(observation, command, power_status, tof_status);

    if (config_.enable_lcd) {
        const cv::Mat view = renderer_.render(frame, observation, command, power_status, tof_status);
        dumpDebugFrames(frame, view, observation);
        pushFrameToLcd(view);
    } else {
        dumpDebugFrames(frame, frame, observation);
    }

    if (shouldDispatchMotion(command, drive_busy)) {
        std::cout << "[AutoCharge] shouldDispatchMotion=yes drive_busy=" << (drive_busy ? "yes" : "no") << " command=" << toString(command.type) << std::endl;
        const bool rc = executeMotion(command);
        std::cout << "[AutoCharge] executeMotion returned=" << (rc ? "true" : "false") << std::endl;
        noteMotionDispatch(command, rc);
    }

    return command;
}

void AutoChargeService::dumpDebugFrames(const cv::Mat& frame,
                                        const cv::Mat& rendered_view,
                                        const MarkerObservation& observation) {
    if (config_.dump_dir.empty()) {
        return;
    }

    const bool should_dump = frame_counter_ == 1 || frame_counter_ % 30 == 0 || observation.found;
    if (!should_dump) {
        return;
    }

    std::error_code error_code;
    std::filesystem::create_directories(config_.dump_dir, error_code);
    const std::string base = config_.dump_dir + "/frame_" + std::to_string(frame_counter_);
    cv::imwrite(base + "_raw.jpg", frame);
    cv::imwrite(base + "_debug.jpg", rendered_view);
}

void AutoChargeService::logCycleSummary(const MarkerObservation& observation,
                                        const MotionCommand& command,
                                        const PowerStatus& power_status,
                                        const TofStatus& tof_status) {
    const bool should_log = frame_counter_ <= 5
        || frame_counter_ % 30 == 0
        || command.phase != last_phase_
        || command.type != last_motion_type_
        || observation.found != last_marker_found_
        || command.reason != last_reason_;

    if (!should_log) {
        return;
    }

    std::ostringstream stream;
    stream << "[AutoCharge] frame=" << frame_counter_
           << " marker=" << (observation.found ? "found" : "lost");

    if (observation.found) {
        stream << " id=" << observation.marker_id
               << " x=" << std::fixed << std::setprecision(3) << observation.normalized_x
               << " area=" << std::setprecision(3) << observation.area_ratio;
        stream << " decoded=" << (observation.decoded ? "yes" : "no");
        if (observation.pose_valid) {
            stream << " dist_m=" << std::setprecision(3) << observation.distance_m;
        }
    }

    stream << " phase=" << toString(command.phase)
           << " motion=" << toString(command.type);

    if (command.type == MotionType::Rotate) {
        stream << " rotate_deg=" << std::setprecision(2) << command.rotate_deg;
    } else if (command.type == MotionType::Forward) {
        stream << " forward_mm=" << std::setprecision(1) << command.forward_mm;
    } else if (command.type == MotionType::Backward) {
        stream << " backward_mm=" << std::setprecision(1) << command.forward_mm;
    }

    stream << " reason=" << command.reason;

    if (power_status.valid) {
        stream << " power=" << power_status.source
               << " V=" << std::setprecision(2) << power_status.voltage_v
               << " I=" << std::setprecision(3) << power_status.current_a
               << " charging=" << (power_status.is_charging ? "yes" : "no");
    } else {
        stream << " power=unavailable";
    }

    if (tof_status.valid) {
        stream << " tof="
               << (tof_status.left_valid ? std::to_string(tof_status.left_mm) : std::string("--"))
               << "/"
               << (tof_status.right_valid ? std::to_string(tof_status.right_mm) : std::string("--"))
               << "mm";
    }

    std::cout << stream.str() << std::endl;

    last_phase_ = command.phase;
    last_motion_type_ = command.type;
    last_marker_found_ = observation.found;
    last_reason_ = command.reason;
}

bool AutoChargeService::executeMotion(const MotionCommand& command) {
    switch (command.type) {
        case MotionType::Rotate:
            std::cout << "[AutoCharge] calling DriveControl::goRotate deg=" << command.rotate_deg << std::endl;
            {
                const bool r = DriveControl::goRotate(
                    command_id_++,
                    static_cast<float>(applyRotateCompensation(command.rotate_deg, config_.motion)),
                    true,
                    command.phase == DockingPhase::Searching ? config_.motion.search_rotate_speed : config_.motion.align_rotate_speed,
                    true,
                    true);
                std::cout << "[AutoCharge] DriveControl::goRotate rc=" << (r ? 1 : 0) << std::endl;
                return r;
            }
        case MotionType::Forward:
            std::cout << "[AutoCharge] calling DriveControl::goDistance forward_mm=" << command.forward_mm << std::endl;
            {
                const bool r = DriveControl::goDistance(
                    command_id_++,
                    static_cast<std::uint16_t>(command.forward_mm),
                    static_cast<std::uint8_t>(config_.motion.forward_speed),
                    true,
                    true);
                std::cout << "[AutoCharge] DriveControl::goDistance rc=" << (r ? 1 : 0) << std::endl;
                return r;
            }
        case MotionType::Backward:
            std::cout << "[AutoCharge] calling DriveControl::goDistance backward_mm=" << command.forward_mm << std::endl;
            {
                const bool r = DriveControl::goDistance(
                    command_id_++,
                    static_cast<std::uint16_t>(command.forward_mm),
                    static_cast<std::uint8_t>(config_.motion.reverse_speed),
                    false,
                    true);
                std::cout << "[AutoCharge] DriveControl::goDistance rc=" << (r ? 1 : 0) << std::endl;
                return r;
            }
        case MotionType::Stop:
            std::cout << "[AutoCharge] calling DriveControl::Abort" << std::endl;
            DriveControl::Abort();
            return true;
        case MotionType::None:
            return true;
    }
    return false;
}

void AutoChargeService::pushFrameToLcd(const cv::Mat& view) {
    if (!lcd_ready_ || view.empty()) {
        return;
    }

    cv::Mat rgb;
    cv::cvtColor(view, rgb, cv::COLOR_BGR2RGB);

    // Ensure frame matches LCD expected resolution (from config)
    cv::Mat lcd_frame;
    // Some build targets may not expose LCD_WIDTH/LCD_HEIGHT macros; use explicit values.
    const int lcd_w = config_.lcd_width;
    const int lcd_h = config_.lcd_height;
    if (rgb.cols != lcd_w || rgb.rows != lcd_h) {
        cv::resize(rgb, lcd_frame, cv::Size(lcd_w, lcd_h), 0.0, 0.0, cv::INTER_AREA);
    } else {
        lcd_frame = rgb;
    }

    if (!lcd_frame.isContinuous()) {
        lcd_frame = lcd_frame.clone();
    }

    // Mutex to serialize LCD writes and avoid driver-level concurrency issues.
    static std::mutex s_lcd_mutex;

    const size_t buf_size = static_cast<size_t>(LcdControl::getBufferSize());
    if (lcd_buffer_.size() < buf_size) lcd_buffer_.resize(buf_size);
    // Prepare LcdData
    LcdData data;
    data.side = static_cast<decltype(data.side)>(config_.lcd_side == 0 ? 0 : 1);
    data.buffer = lcd_buffer_.data();

    // Convert and write under mutex. Use legacy LcdBufferFrom24Bit when
    // `LCD_WIDTH` is defined (older local headers); otherwise use SDK toLcdBuffer.
    {
        std::lock_guard<std::mutex> lock(s_lcd_mutex);
#ifdef LCD_WIDTH
        LcdControl::LcdBufferFrom24Bit(lcd_buffer_.data(), lcd_frame.data);
#else
        LcdControl::toLcdBuffer(lcd_buffer_.data(), lcd_frame.data, false);
#endif
        const int8_t write_rc = LcdControl::writeLcd(&data);
        std::cout << "[AutoCharge] writeLcd rc=" << static_cast<int>(write_rc)
                  << " bufferSize=" << buf_size
                  << " colorDepth=" << static_cast<int>(LcdControl::getColorDepth())
                  << std::endl;
        // Optional throttle to avoid overdriving the SPI driver; set via env var AUTOCHARGE_LCD_THROTTLE_MS
        const char* throttle_env = std::getenv("AUTOCHARGE_LCD_THROTTLE_MS");
        if (throttle_env) {
            try {
                const int ms = std::stoi(throttle_env);
                if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            } catch (...) {}
        }
    }

    // Optional diagnostic: if asked, force max brightness and a solid white fill
    const char* diag = std::getenv("AUTOCHARGE_LCD_DIAG");
    if (diag && std::string(diag) == "1") {
        LcdControl::setBrightness(10);
        LcdControl::LcdColorFill(static_cast<LcdSide>(data.side), 255, 255, 255);
        std::cout << "[AutoCharge] LCD diagnostic: setBrightness=10 and LcdColorFill(white) called" << std::endl;
    }
}

void AutoChargeService::shutdown() {
    if (camera_) {
        camera_->stopVideo();
        camera_.reset();
    }
    if (drive_ready_) {
        DriveControl::Abort();
        DriveControl::dispose(true);
        drive_ready_ = false;
        pending_drive_command_ = false;
    }
    if (lcd_ready_) {
        LcdControl::dispose();
        lcd_ready_ = false;
    }
}

}  // namespace doly::autocharge