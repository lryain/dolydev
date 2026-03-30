#pragma once

#include "autocharge/types.hpp"

namespace doly::autocharge {

class DockingPlanner {
public:
    explicit DockingPlanner(DockingPlannerConfig config = {});

    MotionCommand plan(const MarkerObservation& observation,
                       const PowerStatus& power_status,
                       const TofStatus& tof_status,
                       bool drive_busy);

private:
    enum class PlannerState {
        Searching,
        Aligning,
        Approaching,
        Reacquire,
        TofAlign,
        RotateToReverse,
        ReverseDock,
        VerifyCharging,
        RetryEscape,
        RetryRotateFront,
        Docked,
        Failed,
    };

    DockingPlannerConfig config_;
    PlannerState state_ = PlannerState::Searching;
    bool command_in_flight_ = false;
    bool command_started_ = false;
    int lost_counter_ = 0;
    int marker_lost_streak_ = 0;
    int command_wait_cycles_ = 0;
    int align_stable_counter_ = 0;
    int approach_stable_counter_ = 0;
    int tof_stable_counter_ = 0;
    int reacquire_counter_ = 0;
    int verify_counter_ = 0;
    int retry_count_ = 0;
    bool last_seen_valid_ = false;
    bool last_seen_near_front_ = false;
    double last_seen_x_ = 0.0;
    double pending_motion_mm_ = 0.0;
    double reverse_progress_mm_ = 0.0;
};

}  // namespace doly::autocharge