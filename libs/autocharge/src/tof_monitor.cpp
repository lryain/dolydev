#include "autocharge/tof_monitor.hpp"

#include "vl6180_pi.h"

#include <algorithm>

namespace doly::autocharge {

TofMonitor::TofMonitor() = default;

TofMonitor::~TofMonitor() {
    closeDirectSensors();
}

bool TofMonitor::initialize() {
    return true;
}

bool TofMonitor::openDirectSensors(const SensorServiceConfig& sensor_config) const {
    if (direct_ready_) {
        return true;
    }
    if (!sensor_config.enable_cpp_tof) {
        return false;
    }

    closeDirectSensors();

    left_handle_ = vl6180_initialise_address(sensor_config.tof_i2c_bus, sensor_config.tof_left_address);
    if (left_handle_ < 0) {
        closeDirectSensors();
        return false;
    }
    if (vl6180_set_offset(left_handle_, sensor_config.tof_left_offset_mm) != 0) {
        closeDirectSensors();
        return false;
    }
    if (vl6180_start_range_continuous(left_handle_, sensor_config.tof_continuous_period_ms) != 0) {
        closeDirectSensors();
        return false;
    }

    right_handle_ = vl6180_initialise_address(sensor_config.tof_i2c_bus, sensor_config.tof_right_address);
    if (right_handle_ < 0) {
        closeDirectSensors();
        return false;
    }
    if (vl6180_set_offset(right_handle_, sensor_config.tof_right_offset_mm) != 0) {
        closeDirectSensors();
        return false;
    }
    if (vl6180_start_range_continuous(right_handle_, sensor_config.tof_continuous_period_ms) != 0) {
        closeDirectSensors();
        return false;
    }

    direct_ready_ = true;
    return true;
}

void TofMonitor::closeDirectSensors() const {
    if (left_handle_ >= 0) {
        vl6180_stop_range_continuous(left_handle_);
        vl6180_close(left_handle_);
        left_handle_ = -1;
    }
    if (right_handle_ >= 0) {
        vl6180_stop_range_continuous(right_handle_);
        vl6180_close(right_handle_);
        right_handle_ = -1;
    }
    direct_ready_ = false;
}

TofStatus TofMonitor::readDirect(const SensorServiceConfig& sensor_config) const {
    TofStatus status;
    if (!openDirectSensors(sensor_config)) {
        return status;
    }

    const int timeout_ms = std::max(sensor_config.tof_read_timeout_ms,
                                    sensor_config.tof_continuous_period_ms * 10);
    const int left_mm = vl6180_get_distance_continuous(left_handle_, timeout_ms);
    const int right_mm = vl6180_get_distance_continuous(right_handle_, timeout_ms);

    if (left_mm >= 0) {
        status.left_valid = true;
        status.left_mm = static_cast<std::uint16_t>(left_mm);
    }
    if (right_mm >= 0) {
        status.right_valid = true;
        status.right_mm = static_cast<std::uint16_t>(right_mm);
    }

    status.valid = status.left_valid || status.right_valid;
    if (status.valid) {
        const std::uint16_t left_value = status.left_valid ? status.left_mm : status.right_mm;
        const std::uint16_t right_value = status.right_valid ? status.right_mm : status.left_mm;
        status.min_distance_mm = std::min(left_value, right_value);
        status.obstacle_detected = status.min_distance_mm > 0 && status.min_distance_mm < 120;
    }
    if (status.left_valid && status.right_valid) {
        status.balance_error_mm = static_cast<int>(status.left_mm) - static_cast<int>(status.right_mm);
    }
    status.source = "cpp_vl6180_pi";
    return status;
}

TofStatus TofMonitor::read(const SensorServiceConfig& sensor_config) const {
    return readDirect(sensor_config);
}

}  // namespace doly::autocharge