#include "autocharge/docking_planner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace doly::autocharge {

namespace {

constexpr int kCommandStartGraceCycles = 10;

double clampAbs(double value, double limit) {
    return std::clamp(value, -limit, limit);
}

MotionCommand makeHoldCommand(DockingPhase phase, const std::string& reason, bool stop = false) {
    MotionCommand cmd;
    cmd.phase = phase;
    cmd.type = stop ? MotionType::Stop : MotionType::None;
    cmd.should_stop = stop;
    cmd.reason = reason;
    return cmd;
}

MotionCommand makeRotateCommand(DockingPhase phase, double rotate_deg, const std::string& reason) {
    MotionCommand cmd;
    cmd.phase = phase;
    cmd.type = MotionType::Rotate;
    cmd.rotate_deg = rotate_deg;
    cmd.reason = reason;
    return cmd;
}

MotionCommand makeLinearCommand(DockingPhase phase, MotionType type, double distance_mm, const std::string& reason) {
    MotionCommand cmd;
    cmd.phase = phase;
    cmd.type = type;
    cmd.forward_mm = distance_mm;
    cmd.reason = reason;
    return cmd;
}

MotionCommand makeCompletedCommand(DockingPhase phase, const std::string& reason) {
    MotionCommand cmd;
    cmd.phase = phase;
    cmd.type = MotionType::Stop;
    cmd.should_stop = true;
    cmd.completed = true;
    cmd.reason = reason;
    return cmd;
}

}  // namespace

DockingPlanner::DockingPlanner(DockingPlannerConfig config)
    : config_(config) {}

MotionCommand DockingPlanner::plan(const MarkerObservation& observation,
                                   const PowerStatus& power_status,
                                   const TofStatus& tof_status,
                                   bool drive_busy) {
    auto clearInFlight = [&]() {
        command_in_flight_ = false;
        command_started_ = false;
        command_wait_cycles_ = 0;
        pending_motion_mm_ = 0.0;
    };

    auto resetVisualStability = [&]() {
        align_stable_counter_ = 0;
        approach_stable_counter_ = 0;
    };

    auto beginInFlight = [&](double pending_motion_mm = 0.0) {
        command_in_flight_ = true;
        command_started_ = false;
        command_wait_cycles_ = 0;
        pending_motion_mm_ = pending_motion_mm;
    };

    auto markInFlightRunning = [&]() {
        command_started_ = true;
        command_wait_cycles_ = 0;
    };

    auto waitingForCommandStart = [&]() {
        ++command_wait_cycles_;
        return command_wait_cycles_ <= kCommandStartGraceCycles;
    };

    auto enterReacquire = [&]() {
        resetVisualStability();
        state_ = PlannerState::Reacquire;
        reacquire_counter_ = 0;
    };

    auto reacquireRotateDeg = [&]() {
        const double direction = std::abs(last_seen_x_) >= 0.01 ? (last_seen_x_ > 0.0 ? 1.0 : -1.0) : 1.0;
        return direction * config_.reacquire_rotate_deg;
    };

    const char* ac_dbg = std::getenv("AUTOCHARGE_MARKER_DEBUG");
    const bool ignore_charging_debug = (ac_dbg && std::string(ac_dbg) == "1");

    if (power_status.valid && power_status.is_charging && !ignore_charging_debug) {
        clearInFlight();
        state_ = PlannerState::Docked;
        return makeCompletedCommand(DockingPhase::Docked, "charging-current-detected");
    }

    const double abs_error = std::abs(observation.normalized_x);
    const double effective_distance_m = observation.pose_valid ? observation.distance_m : -1.0;
    const bool near_front = (effective_distance_m > 0.0 && effective_distance_m <= config_.tof_align_trigger_distance_m)
        || observation.area_ratio >= config_.close_area_ratio;
    const bool tof_pair_valid = tof_status.valid
        && tof_status.left_valid
        && tof_status.right_valid
        && tof_status.left_mm < 250
        && tof_status.right_mm < 250;
    const double active_align_threshold = near_front ? config_.near_align_threshold : config_.align_threshold;
    const double active_rotate_gain = near_front ? config_.near_rotate_gain_deg : config_.rotate_gain_deg;
    const double active_max_rotate_deg = near_front ? config_.near_max_rotate_deg : config_.max_rotate_deg;

    if (observation.found) {
        marker_lost_streak_ = 0;
        last_seen_valid_ = true;
        last_seen_x_ = observation.normalized_x;
        last_seen_near_front_ = near_front;
    } else {
        ++marker_lost_streak_;
    }

    for (int guard = 0; guard < 12; ++guard) {
        switch (state_) {
            case PlannerState::Searching:
                resetVisualStability();
                tof_stable_counter_ = 0;
                verify_counter_ = 0;
                if (!observation.found) {
                    ++lost_counter_;
                    return makeRotateCommand(DockingPhase::Searching,
                                             config_.search_rotate_deg,
                                             lost_counter_ > 3 ? "marker-lost-continue-search" : "marker-not-found");
                }
                lost_counter_ = 0;
                state_ = PlannerState::Aligning;
                continue;

            case PlannerState::Aligning:
                if (!observation.found) {
                    if (last_seen_near_front_ && tof_pair_valid) {
                        state_ = PlannerState::TofAlign;
                        continue;
                    }
                    enterReacquire();
                    continue;
                }
                if (abs_error > active_align_threshold) {
                    align_stable_counter_ = 0;
                    return makeRotateCommand(DockingPhase::Aligning,
                                             clampAbs(observation.normalized_x * active_rotate_gain, active_max_rotate_deg),
                                             near_front ? "near-horizontal-error" : "horizontal-error-too-large");
                }

                ++align_stable_counter_;
                if (align_stable_counter_ < config_.align_stable_cycles) {
                    return makeHoldCommand(DockingPhase::Aligning,
                                           near_front ? "align-near-stabilizing" : "align-stabilizing");
                }

                align_stable_counter_ = 0;
                state_ = near_front ? PlannerState::TofAlign : PlannerState::Approaching;
                continue;

            case PlannerState::Approaching:
                if (!observation.found) {
                    if (last_seen_near_front_ && tof_pair_valid) {
                        state_ = PlannerState::TofAlign;
                        continue;
                    }
                    enterReacquire();
                    continue;
                }
                if (abs_error > config_.align_threshold) {
                    approach_stable_counter_ = 0;
                    state_ = PlannerState::Aligning;
                    continue;
                }
                if (near_front) {
                    approach_stable_counter_ = 0;
                    state_ = PlannerState::TofAlign;
                    continue;
                }

                ++approach_stable_counter_;
                if (approach_stable_counter_ < config_.approach_stable_cycles) {
                    return makeHoldCommand(DockingPhase::Approaching, "approach-visual-stabilizing");
                }

                approach_stable_counter_ = 0;
                if (effective_distance_m > 0.0) {
                    const double remaining_mm = (effective_distance_m - config_.tof_align_trigger_distance_m) * 1000.0;
                    return makeLinearCommand(
                        DockingPhase::Approaching,
                        MotionType::Forward,
                        std::clamp(remaining_mm * config_.approach_gain,
                                   config_.min_forward_step_mm,
                                   config_.max_forward_step_mm),
                        "pose-distance-approach");
                }
                return makeLinearCommand(DockingPhase::Approaching,
                                         MotionType::Forward,
                                         config_.min_forward_step_mm,
                                         "area-based-approach");

            case PlannerState::Reacquire:
                if (observation.found) {
                    reacquire_counter_ = 0;
                    state_ = (near_front && tof_pair_valid) ? PlannerState::TofAlign : PlannerState::Aligning;
                    continue;
                }
                if (!last_seen_valid_) {
                    state_ = PlannerState::Searching;
                    continue;
                }
                ++reacquire_counter_;
                if (reacquire_counter_ > config_.reacquire_max_cycles) {
                    state_ = PlannerState::Searching;
                    continue;
                }
                return makeRotateCommand(DockingPhase::Reacquire,
                                         reacquireRotateDeg(),
                                         last_seen_near_front_ ? "marker-reacquire-near" : "marker-reacquire");

            case PlannerState::TofAlign:
                if (observation.found && abs_error > config_.near_align_threshold) {
                    tof_stable_counter_ = 0;
                    state_ = PlannerState::Aligning;
                    continue;
                }
                if (!tof_pair_valid) {
                    tof_stable_counter_ = 0;
                    if (!observation.found && marker_lost_streak_ > 1) {
                        enterReacquire();
                        continue;
                    }
                    return makeHoldCommand(DockingPhase::TofAlign, "waiting-for-tof-pair");
                }
                if (std::abs(tof_status.balance_error_mm) > config_.tof_balance_tolerance_mm) {
                    tof_stable_counter_ = 0;
                    return makeRotateCommand(DockingPhase::TofAlign,
                                             clampAbs(-static_cast<double>(tof_status.balance_error_mm) * config_.tof_balance_rotate_gain_deg,
                                                      config_.tof_max_rotate_deg),
                                             "tof-balance-correction");
                }
                ++tof_stable_counter_;
                if (tof_stable_counter_ < config_.tof_stable_cycles) {
                    return makeHoldCommand(DockingPhase::TofAlign, "tof-align-stabilizing");
                }
                tof_stable_counter_ = 0;
                state_ = PlannerState::RotateToReverse;
                clearInFlight();
                continue;

            case PlannerState::RotateToReverse:
                if (command_in_flight_) {
                    if (drive_busy) {
                        markInFlightRunning();
                        return makeHoldCommand(DockingPhase::RotateToReverse, "rotate-180-running");
                    }

                    if (!command_started_) {
                        if (waitingForCommandStart()) {
                            return makeHoldCommand(DockingPhase::RotateToReverse, "waiting-rotate-180-start");
                        }
                        clearInFlight();
                        continue;
                    }

                    clearInFlight();
                    reverse_progress_mm_ = 0.0;
                    state_ = PlannerState::ReverseDock;
                    continue;
                }
                if (drive_busy) {
                    return makeHoldCommand(DockingPhase::RotateToReverse, "waiting-drive-idle");
                }
                beginInFlight();
                return makeRotateCommand(DockingPhase::RotateToReverse,
                                         config_.rotate_to_reverse_deg,
                                         "rotate-180-to-reverse");

            case PlannerState::ReverseDock:
                if (command_in_flight_) {
                    if (drive_busy) {
                        markInFlightRunning();
                        return makeHoldCommand(DockingPhase::ReverseDock, "reverse-step-running");
                    }

                    if (!command_started_) {
                        if (waitingForCommandStart()) {
                            return makeHoldCommand(DockingPhase::ReverseDock, "waiting-reverse-step-start");
                        }
                        clearInFlight();
                        continue;
                    }

                    const double completed_step_mm = pending_motion_mm_ > 0.0 ? pending_motion_mm_ : config_.reverse_step_mm;
                    clearInFlight();
                    reverse_progress_mm_ += completed_step_mm;
                    if (reverse_progress_mm_ >= config_.reverse_contact_distance_mm) {
                        state_ = PlannerState::VerifyCharging;
                        verify_counter_ = 0;
                        continue;
                    }
                }
                if (drive_busy) {
                    return makeHoldCommand(DockingPhase::ReverseDock, "waiting-drive-idle");
                }
                if (reverse_progress_mm_ >= config_.reverse_contact_distance_mm) {
                    state_ = PlannerState::VerifyCharging;
                    verify_counter_ = 0;
                    continue;
                }
                beginInFlight(config_.reverse_step_mm);
                return makeLinearCommand(DockingPhase::ReverseDock,
                                         MotionType::Backward,
                                         config_.reverse_step_mm,
                                         "reverse-step");

            case PlannerState::VerifyCharging:
                if (verify_counter_ < config_.reverse_verify_cycles) {
                    ++verify_counter_;
                    return makeHoldCommand(DockingPhase::VerifyCharging, "waiting-for-charge-current");
                }
                if (retry_count_ >= config_.max_retry_count) {
                    state_ = PlannerState::Failed;
                    continue;
                }
                ++retry_count_;
                state_ = PlannerState::RetryEscape;
                clearInFlight();
                continue;

            case PlannerState::RetryEscape:
                if (command_in_flight_) {
                    if (drive_busy) {
                        markInFlightRunning();
                        return makeHoldCommand(DockingPhase::RetryEscape, "retry-escape-running");
                    }

                    if (!command_started_) {
                        if (waitingForCommandStart()) {
                            return makeHoldCommand(DockingPhase::RetryEscape, "waiting-retry-escape-start");
                        }
                        clearInFlight();
                        continue;
                    }

                    clearInFlight();
                    state_ = PlannerState::RetryRotateFront;
                    continue;
                }
                if (drive_busy) {
                    return makeHoldCommand(DockingPhase::RetryEscape, "waiting-drive-idle");
                }
                beginInFlight(config_.retry_escape_distance_mm);
                return makeLinearCommand(DockingPhase::RetryEscape,
                                         MotionType::Forward,
                                         config_.retry_escape_distance_mm,
                                         "retry-escape-forward");

            case PlannerState::RetryRotateFront:
                if (command_in_flight_) {
                    if (drive_busy) {
                        markInFlightRunning();
                        return makeHoldCommand(DockingPhase::RetryRotateFront, "retry-rotate-front-running");
                    }

                    if (!command_started_) {
                        if (waitingForCommandStart()) {
                            return makeHoldCommand(DockingPhase::RetryRotateFront, "waiting-retry-rotate-start");
                        }
                        clearInFlight();
                        continue;
                    }

                    clearInFlight();
                    state_ = PlannerState::Searching;
                    lost_counter_ = 0;
                    reverse_progress_mm_ = 0.0;
                    continue;
                }
                if (drive_busy) {
                    return makeHoldCommand(DockingPhase::RetryRotateFront, "waiting-drive-idle");
                }
                beginInFlight();
                return makeRotateCommand(DockingPhase::RetryRotateFront,
                                         config_.rotate_to_reverse_deg,
                                         "rotate-back-to-front");

            case PlannerState::Docked:
                resetVisualStability();
                clearInFlight();
                return makeCompletedCommand(DockingPhase::Docked, "charging-current-detected");

            case PlannerState::Failed:
                resetVisualStability();
                clearInFlight();
                return makeCompletedCommand(DockingPhase::Failed, "max-retry-exceeded");
        }
    }

    return makeCompletedCommand(DockingPhase::Failed, "planner-loop-guard-hit");
}

}  // namespace doly::autocharge