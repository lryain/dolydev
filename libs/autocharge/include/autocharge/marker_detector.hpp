#pragma once

#include "autocharge/types.hpp"

#include <opencv2/objdetect/aruco_detector.hpp>

namespace doly::autocharge {

class MarkerDetector {
public:
    MarkerDetector(int target_marker_id, float marker_size_m, CameraIntrinsics intrinsics, int max_upscale = 1);

    MarkerObservation detect(const cv::Mat& bgr_frame) const;

private:
    int target_marker_id_;
    float marker_size_m_;
    CameraIntrinsics intrinsics_;
    cv::aruco::ArucoDetector detector_;
    int max_upscale_ = 1;
};

}  // namespace doly::autocharge