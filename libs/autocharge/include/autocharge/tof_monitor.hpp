#pragma once

#include "autocharge/types.hpp"

#include <string>

namespace doly::autocharge {

class TofMonitor {
public:
    TofMonitor();
    ~TofMonitor();

    bool initialize();
    TofStatus read(const SensorServiceConfig& sensor_config) const;

private:
    bool openDirectSensors(const SensorServiceConfig& sensor_config) const;
    void closeDirectSensors() const;
    TofStatus readDirect(const SensorServiceConfig& sensor_config) const;

    mutable int left_handle_ = -1;
    mutable int right_handle_ = -1;
    mutable bool direct_ready_ = false;
};

}  // namespace doly::autocharge