#pragma once

#include "autocharge/types.hpp"

#include <string>

namespace doly::autocharge {

class PowerMonitor {
public:
    PowerMonitor();
    ~PowerMonitor();

    bool initialize();
    PowerStatus read(const DockingPlannerConfig& config);

private:
    PowerStatus finalizeChargingState(PowerStatus status, const DockingPlannerConfig& config);
    bool openSharedState();
    void closeSharedState();
    PowerStatus readSharedState(const DockingPlannerConfig& config) const;
    PowerStatus readHwmon(const DockingPlannerConfig& config) const;
    std::string findIna219Path() const;
    int readSysfsValue(const std::string& path) const;

    int shm_fd_ = -1;
    void* shm_ptr_ = nullptr;
    std::string ina219_path_;
    int charging_confirm_counter_ = 0;
};

}  // namespace doly::autocharge