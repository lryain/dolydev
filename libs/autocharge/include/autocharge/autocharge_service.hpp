#pragma once

#include "autocharge/debug_renderer.hpp"
#include "autocharge/docking_planner.hpp"
#include "autocharge/marker_detector.hpp"
#include "autocharge/power_monitor.hpp"
#include "autocharge/tof_monitor.hpp"
#include "autocharge/types.hpp"

#include <memory>
#include <chrono>
#include <string>

class PiCamera;

namespace doly::autocharge {

class AutoChargeService {
public:
    explicit AutoChargeService(ServiceConfig config);
    ~AutoChargeService();

    bool initialize();
    int run();
    void shutdown();

private:
    bool initializeCamera();
    bool initializeDrive();
    bool initializeLcd();
    bool isDriveBusy();
    bool shouldDispatchMotion(const MotionCommand& command, bool drive_busy) const;
    void noteMotionDispatch(const MotionCommand& command, bool accepted);
    MotionCommand executeCycle(const cv::Mat& frame);
    bool executeMotion(const MotionCommand& command);
    void dumpDebugFrames(const cv::Mat& frame,
                         const cv::Mat& rendered_view,
                         const MarkerObservation& observation);
    void logCycleSummary(const MarkerObservation& observation,
                         const MotionCommand& command,
                         const PowerStatus& power_status,
                         const TofStatus& tof_status);
    void pushFrameToLcd(const cv::Mat& view);

    ServiceConfig config_;
    std::unique_ptr<PiCamera> camera_;
    std::unique_ptr<MarkerDetector> detector_;
    std::unique_ptr<DockingPlanner> planner_;
    std::unique_ptr<PowerMonitor> power_monitor_;
    std::unique_ptr<TofMonitor> tof_monitor_;
    DebugRenderer renderer_;

    bool drive_ready_ = false;
    bool lcd_ready_ = false;
    bool camera_ready_ = false;
    bool pending_drive_command_ = false;
    std::chrono::steady_clock::time_point pending_drive_command_since_{};
    std::uint16_t command_id_ = 1;
    std::size_t frame_counter_ = 0;
    std::vector<uint8_t> lcd_buffer_;
    DockingPhase last_phase_ = DockingPhase::Failed;
    MotionType last_motion_type_ = MotionType::None;
    bool last_marker_found_ = false;
    std::string last_reason_;
};

}  // namespace doly::autocharge