#include "autocharge/marker_detector.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>
#include <iostream>

using doly::autocharge::MarkerDetector;
using doly::autocharge::MarkerObservation;
using doly::autocharge::CameraIntrinsics;

namespace {

bool expect(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "[FAIL] " << name << std::endl;
        return false;
    }
    std::cout << "[PASS] " << name << std::endl;
    return true;
}

} // namespace

int main() {
    bool ok = true;

    const int width = 400;
    const int height = 400;
    MarkerDetector detector(23, 0.12f, CameraIntrinsics::createApproximate(width, height));

    // Positive case: draw a large printed-sign-like rectangle and expect detection
    {
        cv::Mat canvas = cv::Mat::zeros(height, width, CV_8UC3);
        // Draw a large white rectangle on black background (so threshold finds the rectangle)
        cv::rectangle(canvas, cv::Point(100,80), cv::Point(300,320), cv::Scalar(255,255,255), cv::FILLED);

        MarkerObservation obs = detector.detect(canvas);
        ok &= expect(obs.found, "positive: printed-sign like rectangle found");
    }

    // Negative case: draw an irregular blob (simulate hand) and expect no detection
    {
        cv::Mat canvas = cv::Mat::ones(height, width, CV_8UC3) * 255;
        // draw an irregular filled contour
        std::vector<cv::Point> poly = {{50,200},{90,160},{140,150},{170,170},{200,220},{180,260},{120,300},{80,280}};
        std::vector<std::vector<cv::Point>> contours = {poly};
        cv::fillPoly(canvas, contours, cv::Scalar(0,0,0));

        MarkerObservation obs = detector.detect(canvas);
        ok &= expect(!obs.found, "negative: irregular blob not detected as marker");
    }

    return ok ? 0 : 1;
}
