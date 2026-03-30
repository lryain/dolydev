#include "autocharge/power_monitor.hpp"

#include "drive/shared_state.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

namespace doly::autocharge {

using SharedState = doly::drive::SharedState;

namespace {

float normalizeCurrentToAmps(float raw_current) {
    const float magnitude = std::fabs(raw_current);
    if (magnitude >= 1000.0f) {
        return raw_current / 1000000.0f;
    }
    if (magnitude >= 20.0f) {
        return raw_current / 1000.0f;
    }
    return raw_current;
}

constexpr int kInvalidSysfsValue = std::numeric_limits<int>::min();

}  // namespace

PowerMonitor::PowerMonitor() = default;

PowerMonitor::~PowerMonitor() {
    closeSharedState();
}

bool PowerMonitor::initialize() {
    const bool shared_ok = openSharedState();
    ina219_path_ = findIna219Path();
    return shared_ok || !ina219_path_.empty();
}

PowerStatus PowerMonitor::read(const DockingPlannerConfig& config) {
    PowerStatus status = readSharedState(config);
    if (!status.valid) {
        status = readHwmon(config);
    }
    return finalizeChargingState(std::move(status), config);
}

PowerStatus PowerMonitor::finalizeChargingState(PowerStatus status, const DockingPlannerConfig& config) {
    if (!status.valid) {
        charging_confirm_counter_ = 0;
        return status;
    }

    status.current_a = normalizeCurrentToAmps(status.current_a);
    const bool charging_candidate = status.voltage_v >= config.charge_detect_voltage_v
        && status.current_a >= config.charge_detect_current_a
        && status.current_a <= config.charge_detect_max_current_a;

    if (charging_candidate) {
        ++charging_confirm_counter_;
    } else {
        charging_confirm_counter_ = 0;
    }

    status.is_charging = charging_confirm_counter_ >= config.charge_detect_confirm_cycles;
    return status;
}

bool PowerMonitor::openSharedState() {
    if (shm_fd_ >= 0) {
        return true;
    }
    shm_fd_ = ::shm_open(doly::drive::SHARED_STATE_NAME, O_RDONLY, 0);
    if (shm_fd_ < 0) {
        shm_fd_ = -1;
        return false;
    }
    shm_ptr_ = ::mmap(nullptr, doly::drive::SHARED_STATE_SIZE, PROT_READ, MAP_SHARED, shm_fd_, 0);
    if (shm_ptr_ == MAP_FAILED) {
        shm_ptr_ = nullptr;
        ::close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    return true;
}

void PowerMonitor::closeSharedState() {
    if (shm_ptr_ != nullptr) {
        ::munmap(shm_ptr_, doly::drive::SHARED_STATE_SIZE);
        shm_ptr_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
}

PowerStatus PowerMonitor::readSharedState(const DockingPlannerConfig& config) const {
    PowerStatus status;
    if (shm_ptr_ == nullptr) {
        return status;
    }

    auto* shared = static_cast<const SharedState*>(shm_ptr_);
    if (shared->magic != 0xD014FEED) {
        return status;
    }

    const std::uint64_t now_ms = SharedState::getCurrentTimeMs();
    const std::uint64_t update_ms = shared->power.update_time_ms.load(std::memory_order_relaxed);
    if (update_ms == 0 || now_ms < update_ms || (now_ms - update_ms) > static_cast<std::uint64_t>(config.shared_state_timeout_ms)) {
        return status;
    }

    status.valid = true;
    status.voltage_v = shared->power.voltage.load(std::memory_order_relaxed);
    status.current_a = shared->power.current.load(std::memory_order_relaxed);
    status.percentage = shared->power.percentage.load(std::memory_order_relaxed);
    status.low_battery = shared->power.low_battery.load(std::memory_order_relaxed);
    status.source = "shared_state";
    return status;
}

PowerStatus PowerMonitor::readHwmon(const DockingPlannerConfig&) const {
    PowerStatus status;
    if (ina219_path_.empty()) {
        return status;
    }

    const int voltage_mv = readSysfsValue(ina219_path_ + "/in1_input");
    const int current_raw = readSysfsValue(ina219_path_ + "/curr1_input");
    if (voltage_mv == kInvalidSysfsValue || current_raw == kInvalidSysfsValue) {
        return status;
    }

    status.valid = true;
    status.voltage_v = static_cast<float>(voltage_mv) / 1000.0f;
    status.current_a = static_cast<float>(current_raw) / 1000000.0f;
    status.percentage = 0;
    status.low_battery = status.voltage_v < 3.5f;
    status.source = "ina219";
    return status;
}

std::string PowerMonitor::findIna219Path() const {
    const std::filesystem::path base_path("/sys/class/hwmon");
    try {
        for (const auto& entry : std::filesystem::directory_iterator(base_path)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto name_path = entry.path() / "name";
            if (!std::filesystem::exists(name_path)) {
                continue;
            }
            std::ifstream stream(name_path);
            std::string name;
            std::getline(stream, name);
            if (name == "ina219") {
                return entry.path().string();
            }
        }
    } catch (...) {
        return "";
    }
    return "";
}

int PowerMonitor::readSysfsValue(const std::string& path) const {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        return kInvalidSysfsValue;
    }
    int value = kInvalidSysfsValue;
    stream >> value;
    return stream.fail() ? kInvalidSysfsValue : value;
}

}  // namespace doly::autocharge