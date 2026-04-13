#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <optional>

namespace doly::vision {

class CaptureModule {
public:
    CaptureModule();

    bool savePhoto(const cv::Mat& frame,
                   const std::string& save_dir,
                   const std::string& filename,
                   int quality = 95);

    bool startVideo(const std::string& save_dir,
                    const std::string& filename,
                    int fps,
                    const cv::Size& frame_size);

    bool appendFrame(const cv::Mat& frame);

    bool stopVideo();

    [[nodiscard]] bool isRecording() const { return recording_; }

private:
    static std::string ensureDir(const std::string& dir);
    static std::string defaultFilename(const std::string& prefix, const std::string& ext);

    cv::VideoWriter writer_;
    bool recording_{false};
};

}  // namespace doly::vision
