#include "autocharge/docking_planner.hpp"

#include <cmath>
#include <iostream>

using doly::autocharge::DockingPhase;
using doly::autocharge::DockingPlanner;
using doly::autocharge::MarkerObservation;
using doly::autocharge::MotionType;
using doly::autocharge::PowerStatus;
using doly::autocharge::TofStatus;

namespace {

bool expect(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "[FAIL] " << name << std::endl;
        return false;
    }
    std::cout << "[PASS] " << name << std::endl;
    return true;
}

}  // namespace

int main() {
    DockingPlanner planner;
    bool ok = true;

    MarkerObservation not_found;
    PowerStatus power;
    TofStatus tof;

    auto search = planner.plan(not_found, power, tof, false);
    ok &= expect(search.type == MotionType::Rotate, "search emits rotate");
    ok &= expect(search.phase == DockingPhase::Searching, "search phase");

    MarkerObservation misaligned;
    misaligned.found = true;
    misaligned.marker_id = 23;
    misaligned.normalized_x = 0.4;
    misaligned.pose_valid = true;
    misaligned.distance_m = 0.45;

    auto align = planner.plan(misaligned, power, tof, false);
    ok &= expect(align.type == MotionType::Rotate, "align emits rotate");
    ok &= expect(align.phase == DockingPhase::Aligning, "align phase");

    MarkerObservation approach_obs = misaligned;
    approach_obs.normalized_x = 0.01;
    approach_obs.distance_m = 0.44;
    auto approach_align_hold = planner.plan(approach_obs, power, tof, false);
    ok &= expect(approach_align_hold.type == MotionType::None, "align stabilize before approach");
    auto approach_hold = planner.plan(approach_obs, power, tof, false);
    ok &= expect(approach_hold.type == MotionType::None, "approach stabilize holds");
    auto approach = planner.plan(approach_obs, power, tof, false);
    ok &= expect(approach.type == MotionType::Forward, "approach emits forward");
    ok &= expect(approach.phase == DockingPhase::Approaching, "approach phase");

    MarkerObservation near_obs = approach_obs;
    near_obs.distance_m = 0.26;
    near_obs.area_ratio = 0.30;

    tof.valid = true;
    tof.left_valid = true;
    tof.right_valid = true;
    tof.left_mm = 145;
    tof.right_mm = 120;
    tof.balance_error_mm = 25;

    auto tof_align = planner.plan(near_obs, power, tof, false);
    ok &= expect(tof_align.type == MotionType::Rotate, "tof align emits rotate");
    ok &= expect(tof_align.phase == DockingPhase::TofAlign, "tof align phase");

    tof.left_mm = 121;
    tof.right_mm = 118;
    tof.balance_error_mm = 3;
    auto tof_hold = planner.plan(near_obs, power, tof, false);
    ok &= expect(tof_hold.phase == DockingPhase::TofAlign, "tof stabilize phase");
    auto rotate_reverse = planner.plan(near_obs, power, tof, false);
    ok &= expect(rotate_reverse.type == MotionType::Rotate, "rotate reverse emits rotate");
    ok &= expect(rotate_reverse.phase == DockingPhase::RotateToReverse, "rotate reverse phase");

    auto rotate_start_grace = planner.plan(near_obs, power, tof, false);
    ok &= expect(rotate_start_grace.type == MotionType::None, "rotate start grace holds");
    ok &= expect(rotate_start_grace.phase == DockingPhase::RotateToReverse, "rotate start grace phase");

    auto reverse_wait = planner.plan(near_obs, power, tof, true);
    ok &= expect(reverse_wait.phase == DockingPhase::RotateToReverse, "rotate busy phase");
    auto reverse_step = planner.plan(near_obs, power, tof, false);
    ok &= expect(reverse_step.type == MotionType::Backward, "reverse emits backward");
    ok &= expect(reverse_step.phase == DockingPhase::ReverseDock, "reverse phase");

    auto reverse_start_grace = planner.plan(near_obs, power, tof, false);
    ok &= expect(reverse_start_grace.type == MotionType::None, "reverse start grace holds");
    ok &= expect(reverse_start_grace.phase == DockingPhase::ReverseDock, "reverse start grace phase");

    power.valid = true;
    power.voltage_v = 4.2f;
    power.current_a = 0.2f;
    power.is_charging = true;
    auto docked = planner.plan(near_obs, power, tof, false);
    ok &= expect(docked.completed, "charging completes planner");
    ok &= expect(docked.phase == DockingPhase::Docked, "docked phase");

    DockingPlanner reacquire_planner;
    MarkerObservation reacquire_obs;
    reacquire_obs.found = true;
    reacquire_obs.marker_id = 23;
    reacquire_obs.normalized_x = -0.30;
    reacquire_obs.pose_valid = true;
    reacquire_obs.distance_m = 0.40;
    auto reacquire_align = reacquire_planner.plan(reacquire_obs, PowerStatus{}, TofStatus{}, false);
    ok &= expect(reacquire_align.phase == DockingPhase::Aligning, "reacquire setup align phase");
    auto reacquire = reacquire_planner.plan(MarkerObservation{}, PowerStatus{}, TofStatus{}, false);
    ok &= expect(reacquire.phase == DockingPhase::Reacquire, "lost marker enters reacquire");
    ok &= expect(reacquire.type == MotionType::Rotate, "reacquire emits rotate");
    ok &= expect(reacquire.rotate_deg < 0.0, "reacquire keeps last direction");

    DockingPlanner near_front_planner;
    MarkerObservation near_front_obs;
    near_front_obs.found = true;
    near_front_obs.marker_id = 23;
    near_front_obs.normalized_x = -0.18;
    near_front_obs.pose_valid = true;
    near_front_obs.distance_m = 0.31;
    near_front_obs.area_ratio = 0.48;

    TofStatus balanced_tof;
    balanced_tof.valid = true;
    balanced_tof.left_valid = true;
    balanced_tof.right_valid = true;
    balanced_tof.left_mm = 124;
    balanced_tof.right_mm = 120;
    balanced_tof.balance_error_mm = 4;

    auto near_front_hold = near_front_planner.plan(near_front_obs, PowerStatus{}, balanced_tof, false);
    ok &= expect(near_front_hold.phase == DockingPhase::Aligning, "near front align stabilize phase");
    auto near_front_tof = near_front_planner.plan(near_front_obs, PowerStatus{}, balanced_tof, false);
    ok &= expect(near_front_tof.phase == DockingPhase::TofAlign, "near front enters tof align");

    balanced_tof.left_mm = 145;
    balanced_tof.right_mm = 120;
    balanced_tof.balance_error_mm = 25;
    auto tof_without_marker = near_front_planner.plan(MarkerObservation{}, PowerStatus{}, balanced_tof, false);
    ok &= expect(tof_without_marker.phase == DockingPhase::TofAlign, "tof align survives marker loss");
    ok &= expect(tof_without_marker.type == MotionType::Rotate, "tof align still corrects after marker loss");

    return ok ? 0 : 1;
}