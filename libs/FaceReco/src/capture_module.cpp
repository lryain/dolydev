#include "doly/vision/capture_module.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace doly::vision {

CaptureModule::CaptureModule() = default;

std::string CaptureModule::ensureDir(const std::string& dir) {
    if (dir.empty()) {
        return ".";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string CaptureModule::defaultFilename(const std::string& prefix, const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << prefix << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ext;
    return oss.str();
}

bool CaptureModule::savePhoto(const cv::Mat& frame,
                              const std::string& save_dir,
                              const std::string& filename,
                              int quality) {
    if (frame.empty()) {
        return false;
    }

    auto dir = ensureDir(save_dir);
    auto name = filename.empty() ? defaultFilename("photo", ".jpg") : filename;
    auto path = std::filesystem::path(dir) / name;

    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(std::min(std::max(quality, 1), 100));

    return cv::imwrite(path.string(), frame, params);
}

bool CaptureModule::startVideo(const std::string& save_dir,
                               const std::string& filename,
                               int fps,
                               const cv::Size& frame_size) {
    if (recording_) {
        return false;
    }

    auto dir = ensureDir(save_dir);
    auto name = filename.empty() ? defaultFilename("video", ".avi") : filename;
    auto path = std::filesystem::path(dir) / name;

    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    if (!writer_.open(path.string(), fourcc, fps, frame_size, true)) {
        recording_ = false;
        return false;
    }

    recording_ = true;
    return true;
}

bool CaptureModule::appendFrame(const cv::Mat& frame) {
    if (!recording_ || frame.empty()) {
        return false;
    }

    writer_.write(frame);
    return true;
}

bool CaptureModule::stopVideo() {
    if (!recording_) {
        return true;
    }

    writer_.release();
    recording_ = false;
    return true;
}

}  // namespace doly::vision
