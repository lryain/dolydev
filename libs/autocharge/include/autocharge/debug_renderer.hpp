#pragma once

#include "autocharge/types.hpp"

#include <opencv2/core.hpp>

namespace doly::autocharge {

class DebugRenderer {
public:
    explicit DebugRenderer(DebugViewConfig config = {});

    cv::Mat render(const cv::Mat& frame,
                   const MarkerObservation& observation,
                   const MotionCommand& command,
                   const PowerStatus& power_status,
                   const TofStatus& tof_status) const;

private:
    DebugViewConfig config_;
};

}  // namespace doly::autocharge