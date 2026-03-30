#include "autocharge/marker_detector.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <iostream>

#include <algorithm>
#include <array>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace doly::autocharge {

namespace {
bool markerDebugEnabled() {
    const char* v = std::getenv("AUTOCHARGE_MARKER_DEBUG");
    return v && std::strcmp(v, "1") == 0;
}

std::string getRoiDumpDir() {
    const char* v = std::getenv("AUTOCHARGE_ROI_DUMP_DIR");
    if (!v || std::strlen(v) == 0) return std::string();
    return std::string(v);
}

std::array<cv::Point2f, 4> orderQuad(const std::array<cv::Point2f, 4>& input) {
    std::array<cv::Point2f, 4> ordered;
    const auto sum_cmp = [](const cv::Point2f& a, const cv::Point2f& b) {
        return (a.x + a.y) < (b.x + b.y);
    };
    const auto diff_cmp = [](const cv::Point2f& a, const cv::Point2f& b) {
        return (a.x - a.y) < (b.x - b.y);
    };

    ordered[0] = *std::min_element(input.begin(), input.end(), sum_cmp);
    ordered[2] = *std::max_element(input.begin(), input.end(), sum_cmp);
    ordered[1] = *std::max_element(input.begin(), input.end(), diff_cmp);
    ordered[3] = *std::min_element(input.begin(), input.end(), diff_cmp);
    return ordered;
}

MarkerObservation buildObservation(const std::vector<cv::Point2f>& corners,
                                   int marker_id,
                                   float marker_size_m,
                                   const CameraIntrinsics& intrinsics,
                                   int frame_width,
                                   int frame_height,
                                   float width_scale = 1.0f,
                                   float height_scale = 1.0f,
                                   int solve_pnp_flag = cv::SOLVEPNP_IPPE_SQUARE) {
    MarkerObservation result;
    if (corners.size() != 4) {
        return result;
    }

    result.found = true;
    result.marker_id = marker_id;
    result.corners = corners;

    cv::Point2f center(0.0f, 0.0f);
    for (const auto& point : corners) {
        center += point;
    }
    center *= 0.25f;
    result.normalized_x = ((center.x / static_cast<float>(frame_width)) - 0.5) * 2.0;
    result.area_ratio = std::fabs(cv::contourArea(corners)) / static_cast<double>(frame_width * frame_height);

    const float object_width = marker_size_m * width_scale;
    const float object_height = marker_size_m * height_scale;
    std::array<cv::Point3f, 4> object_points = {
        cv::Point3f(-object_width / 2.0F,  object_height / 2.0F, 0.0F),
        cv::Point3f( object_width / 2.0F,  object_height / 2.0F, 0.0F),
        cv::Point3f( object_width / 2.0F, -object_height / 2.0F, 0.0F),
        cv::Point3f(-object_width / 2.0F, -object_height / 2.0F, 0.0F),
    };

    result.pose_valid = cv::solvePnP(
        std::vector<cv::Point3f>(object_points.begin(), object_points.end()),
        corners,
        intrinsics.camera_matrix,
        intrinsics.dist_coeffs,
        result.rvec,
        result.tvec,
        false,
        solve_pnp_flag);

    if (result.pose_valid) {
        result.distance_m = cv::norm(result.tvec);
    }
    return result;
}

MarkerObservation detectPrintedDockSign(const cv::Mat& gray,
                                        int frame_width,
                                        int frame_height,
                                        int marker_id,
                                        float marker_size_m,
                                        const CameraIntrinsics& intrinsics,
                                        const cv::aruco::ArucoDetector& detector) {
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);

    cv::Mat binary;
    // Use adaptive threshold to handle varying lighting; fallback to fixed if needed
    try {
        cv::Mat tmp;
        if (blurred.channels() == 3) cv::cvtColor(blurred, tmp, cv::COLOR_BGR2GRAY); else tmp = blurred;
        cv::adaptiveThreshold(tmp, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 51, 10);
    } catch (...) {
        cv::threshold(blurred, binary, 170, 255, cv::THRESH_BINARY);
    }

    // Save binary image for debugging if requested
    const std::string dbg_dir = getRoiDumpDir();
    if (!dbg_dir.empty() && markerDebugEnabled()) {
        try {
            std::filesystem::create_directories(dbg_dir);
            const auto now = std::chrono::system_clock::now();
            const auto s = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            std::ostringstream bf;
            bf << dbg_dir << "/binary_" << s << ".png";
            cv::imwrite(bf.str(), binary);
        } catch (...) {}
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (markerDebugEnabled()) {
        std::cout << "[MarkerDetector] detectPrintedDockSign found_contours=" << contours.size()
                  << " frame=" << frame_width << "x" << frame_height << std::endl;
    }

    const cv::Point2f frame_center(frame_width * 0.5f, frame_height * 0.5f);
    double best_score = -std::numeric_limits<double>::infinity();
    std::vector<cv::Point2f> best_corners;

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < frame_width * frame_height * 0.02 || area > frame_width * frame_height * 0.75) {
            continue;
        }

        // Require contour to be roughly rectangular: approx poly must have 4 vertices and be convex.
        std::vector<cv::Point> approx;
        const double peri = cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, 0.01 * peri, true);
        if (!cv::isContourConvex(approx)) {
            if (markerDebugEnabled()) {
                std::cout << "[MarkerDetector] reject_nonquad approx_pts=" << approx.size() << " area=" << area << std::endl;
            }
            continue;
        }

        // Check solidity (how much of bounding rect is filled by contour). Printed signs are solid.
        const cv::Rect bound = cv::boundingRect(contour);
        const double rect_area = static_cast<double>(bound.width) * static_cast<double>(bound.height);
        const double solidity = rect_area > 0.0 ? (area / rect_area) : 0.0;
        if (solidity < 0.35) {
            if (markerDebugEnabled()) {
                std::cout << "[MarkerDetector] reject_low_solidity solidity=" << solidity << " rect_area=" << rect_area << " area=" << area << std::endl;
            }
            continue;
        }

        const cv::RotatedRect rect = cv::minAreaRect(contour);
        const float short_edge = std::min(rect.size.width, rect.size.height);
        const float long_edge = std::max(rect.size.width, rect.size.height);
        if (short_edge < 80.0f || long_edge <= 0.0f) {
            continue;
        }

        const float aspect_ratio = short_edge / long_edge;
        if (aspect_ratio < 0.55f || aspect_ratio > 0.95f) {
            continue;
        }

        if (markerDebugEnabled()) {
            const auto& c = contour;
            const cv::Rect r = cv::boundingRect(c);
            std::cout << "[MarkerDetector] candidate area=" << area
                      << " aspect_ratio=" << aspect_ratio
                      << " rect=" << r.x << "," << r.y << "," << r.width << "," << r.height
                      << " short_edge=" << short_edge << " long_edge=" << long_edge
                      << std::endl;
        }

        std::array<cv::Point2f, 4> box;
        rect.points(box.data());
        const auto ordered = orderQuad(box);
        const double center_distance = cv::norm(rect.center - frame_center);
        const double score = area - center_distance * 120.0;
        if (score > best_score) {
            best_score = score;
            best_corners.assign(ordered.begin(), ordered.end());
        }
    }

    if (best_corners.empty()) {
        return MarkerObservation{};
    }

    if (markerDebugEnabled()) {
        std::cout << "[MarkerDetector] printed-sign best_score=" << best_score
                  << " corners=" << best_corners.size() << std::endl;
        for (size_t i = 0; i < best_corners.size(); ++i) {
            const auto &p = best_corners[i];
            std::cout << "  corner[" << i << "]=" << p.x << "," << p.y << std::endl;
        }
    }

    // The printed sign uses a portrait page where the ArUco square occupies 2/3 of the width.
    // Try several preprocessing variants on a warped candidate ROI and attempt ArUco decoding there.
    try {
        const int warp_size = 800;
        std::vector<cv::Point2f> src_pts = best_corners;
        std::vector<cv::Point2f> dst_pts = { {0.0f, 0.0f}, {static_cast<float>(warp_size), 0.0f}, {static_cast<float>(warp_size), static_cast<float>(warp_size)}, {0.0f, static_cast<float>(warp_size)} };
        const cv::Mat M = cv::getPerspectiveTransform(src_pts, dst_pts);
        cv::Mat warped;
        cv::warpPerspective(gray, warped, M, cv::Size(warp_size, warp_size));

        struct TryImg { std::string name; cv::Mat img; };
        std::vector<TryImg> tries;
        tries.push_back({"raw", warped});

        // CLAHE
        try {
            cv::Mat clahe;
            if (warped.channels() == 3) cv::cvtColor(warped, clahe, cv::COLOR_BGR2GRAY); else clahe = warped.clone();
            cv::Ptr<cv::CLAHE> c = cv::createCLAHE(2.0, cv::Size(8,8));
            c->apply(clahe, clahe);
            tries.push_back({"clahe", clahe});
        } catch (...) {}

        // adaptive threshold
        try {
            cv::Mat at;
            if (warped.channels() == 3) cv::cvtColor(warped, at, cv::COLOR_BGR2GRAY); else at = warped.clone();
            cv::adaptiveThreshold(at, at, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 11, 2);
            tries.push_back({"adaptive", at});
        } catch (...) {}

        // enlarged (2x, 3x, 4x) and sharpening/inversion variants
        try {
            cv::Mat large2, large3, large4;
            cv::resize(warped, large2, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
            cv::resize(warped, large3, cv::Size(), 3.0, 3.0, cv::INTER_CUBIC);
            cv::resize(warped, large4, cv::Size(), 4.0, 4.0, cv::INTER_CUBIC);
            tries.push_back({"large2", large2});
            tries.push_back({"large3", large3});
            tries.push_back({"large4", large4});

            // sharpen (unsharp mask) on medium upsample
            cv::Mat blursh; cv::GaussianBlur(large2, blursh, cv::Size(0,0), 3);
            cv::Mat sharpen = cv::Mat::zeros(large2.size(), large2.type());
            cv::addWeighted(large2, 1.5, blursh, -0.5, 0, sharpen);
            tries.push_back({"sharpen", sharpen});

            // invert
            cv::Mat inv; cv::bitwise_not(large2, inv);
            tries.push_back({"invert", inv});

            // morphological close to remove small holes
            cv::Mat morph; cv::Mat kern = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
            cv::morphologyEx(large2, morph, cv::MORPH_CLOSE, kern);
            tries.push_back({"morph", morph});
        } catch (...) {}

        // prepare dump dir if needed
        const std::string dump_dir = getRoiDumpDir();
        static int roi_counter = 0;
        for (const auto &ti : tries) {
            std::vector<std::vector<cv::Point2f>> roi_corners;
            std::vector<int> roi_ids;
            // use the provided detector instance to detect markers in the ROI
            detector.detectMarkers(ti.img, roi_corners, roi_ids);

            // dump ROI image for offline inspection when requested
            if (!dump_dir.empty()) {
                try {
                    std::filesystem::create_directories(dump_dir);
                    const auto now = std::chrono::system_clock::now();
                    const auto s = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                    std::ostringstream fname;
                    fname << dump_dir << "/roi_" << std::setw(6) << std::setfill('0') << roi_counter++
                          << "_" << ti.name << "_" << s << ".png";
                    cv::imwrite(fname.str(), ti.img);
                } catch (...) {}
            }
            if (markerDebugEnabled()) {
                std::cout << "[MarkerDetector] roi aruco try=" << ti.name << " ids=" << roi_ids.size() << std::endl;
            }
            for (size_t i = 0; i < roi_ids.size(); ++i) {
                if (roi_ids[i] == marker_id) {
                    std::vector<cv::Point2f> roi_pts = roi_corners[i];
                    double scale = static_cast<double>(ti.img.cols) / static_cast<double>(warp_size);
                    if (scale != 1.0) {
                        for (auto &p : roi_pts) { p.x = static_cast<float>(p.x / scale); p.y = static_cast<float>(p.y / scale); }
                    }
                    cv::Mat Minv = M.inv();
                    std::vector<cv::Point2f> mapped_pts;
                    cv::perspectiveTransform(roi_pts, mapped_pts, Minv);
                    if (mapped_pts.size() == 4) {
                        if (markerDebugEnabled()) std::cout << "[MarkerDetector] roi decoded marker_id=" << marker_id << " via " << ti.name << std::endl;
                        MarkerObservation obs = buildObservation(mapped_pts,
                                                                  marker_id,
                                                                  marker_size_m,
                                                                  intrinsics,
                                                                  frame_width,
                                                                  frame_height);
                        obs.decoded = true;
                        return obs;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        if (markerDebugEnabled()) std::cout << "[MarkerDetector] roi aruco exception=" << e.what() << std::endl;
    }

    return buildObservation(best_corners,
                            marker_id,
                            marker_size_m,
                            intrinsics,
                            frame_width,
                            frame_height,
                            1.5f,
                            2.0f,
                            cv::SOLVEPNP_ITERATIVE);
}

}  // namespace

CameraIntrinsics CameraIntrinsics::createApproximate(int width, int height) {
    CameraIntrinsics intrinsics;
    const double focal = static_cast<double>(std::max(width, height));
    intrinsics.camera_matrix = (cv::Mat_<double>(3, 3)
        << focal, 0.0, width / 2.0,
           0.0, focal, height / 2.0,
           0.0, 0.0, 1.0);
    intrinsics.dist_coeffs = cv::Mat::zeros(1, 5, CV_64F);
    return intrinsics;
}

const char* toString(DockingPhase phase) {
    switch (phase) {
        case DockingPhase::Searching: return "SEARCHING";
        case DockingPhase::Aligning: return "ALIGNING";
        case DockingPhase::Approaching: return "APPROACHING";
        case DockingPhase::Reacquire: return "REACQUIRE";
        case DockingPhase::TofAlign: return "TOF_ALIGN";
        case DockingPhase::RotateToReverse: return "ROTATE_180";
        case DockingPhase::ReverseDock: return "REVERSE_DOCK";
        case DockingPhase::RetryEscape: return "RETRY_ESCAPE";
        case DockingPhase::RetryRotateFront: return "RETRY_ROTATE";
        case DockingPhase::FinalDock: return "FINAL_DOCK";
        case DockingPhase::VerifyCharging: return "VERIFY_CHARGING";
        case DockingPhase::Docked: return "DOCKED";
        case DockingPhase::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

const char* toString(MotionType type) {
    switch (type) {
        case MotionType::None: return "NONE";
        case MotionType::Rotate: return "ROTATE";
        case MotionType::Forward: return "FORWARD";
        case MotionType::Backward: return "BACKWARD";
        case MotionType::Stop: return "STOP";
    }
    return "UNKNOWN";
}

MarkerDetector::MarkerDetector(int target_marker_id, float marker_size_m, CameraIntrinsics intrinsics, int max_upscale)
        : target_marker_id_(target_marker_id),
            marker_size_m_(marker_size_m),
            intrinsics_(std::move(intrinsics)),
            max_upscale_(std::max(1, max_upscale)) {
        const auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
        cv::aruco::DetectorParameters params;
        // Tuning: relax and extend thresholds to improve recall in noisy/low-contrast captures
        params.adaptiveThreshWinSizeMin = 3;
        params.adaptiveThreshWinSizeMax = 101; // allow larger adaptive windows
        params.adaptiveThreshWinSizeStep = 10;
        params.adaptiveThreshConstant = 5; // smaller helps low-contrast
        params.minMarkerPerimeterRate = 0.001f; // allow very small detected perimeters
        params.polygonalApproxAccuracyRate = 0.02f; // polygon approx accuracy
        params.perspectiveRemovePixelPerCell = 4; // finer sampling when removing perspective
        params.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
        // allow detection on weaker borders
        params.maxErroneousBitsInBorderRate = 0.3f;
        cv::aruco::RefineParameters rparams;
        detector_ = cv::aruco::ArucoDetector(dict, params, rparams);
}

MarkerObservation MarkerDetector::detect(const cv::Mat& bgr_frame) const {
    MarkerObservation result;
    if (bgr_frame.empty()) {
        return result;
    }

    cv::Mat gray;
    if (bgr_frame.channels() == 3) {
        cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = bgr_frame;
    }

    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    detector_.detectMarkers(gray, corners, ids);
    if (markerDebugEnabled()) {
        std::cout << "[MarkerDetector] aruco detect found_ids=" << ids.size();
        if (!ids.empty()) {
            std::cout << " ids=[";
            for (size_t i = 0; i < ids.size(); ++i) {
                std::cout << ids[i];
                if (i + 1 < ids.size()) std::cout << ",";
            }
            std::cout << "]";
        }
        std::cout << std::endl;
    }
    if (ids.empty()) {
        // Try multi-scale ArUco detection on upscaled full-frame when marker is small/far
        for (int scale = 2; scale <= max_upscale_; ++scale) {
            try {
                cv::Mat up;
                cv::resize(gray, up, cv::Size(), static_cast<double>(scale), static_cast<double>(scale), cv::INTER_CUBIC);
                std::vector<std::vector<cv::Point2f>> corners_s;
                std::vector<int> ids_s;
                detector_.detectMarkers(up, corners_s, ids_s);
                if (!ids_s.empty()) {
                    for (size_t i = 0; i < ids_s.size(); ++i) {
                        if (ids_s[i] != target_marker_id_) continue;
                        // scale corners back to original image coordinates
                        std::vector<cv::Point2f> mapped = corners_s[i];
                        for (auto &p : mapped) { p.x = p.x / static_cast<float>(scale); p.y = p.y / static_cast<float>(scale); }
                        MarkerObservation obs = buildObservation(mapped,
                                                                  ids_s[i],
                                                                  marker_size_m_,
                                                                  intrinsics_,
                                                                  bgr_frame.cols,
                                                                  bgr_frame.rows);
                        obs.decoded = true;
                        if (markerDebugEnabled()) std::cout << "[MarkerDetector] upscale detect scale=" << scale << " id=" << ids_s[i] << std::endl;
                        return obs;
                    }
                }
            } catch (...) {}
        }

        // fallback to printed-sign heuristic
        return detectPrintedDockSign(gray,
                                     bgr_frame.cols,
                                     bgr_frame.rows,
                                     target_marker_id_,
                                     marker_size_m_,
                                     intrinsics_,
                                     detector_);
    }

    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] != target_marker_id_) {
            continue;
        }

        MarkerObservation obs = buildObservation(corners[index],
                                                  ids[index],
                                                  marker_size_m_,
                                                  intrinsics_,
                                                  bgr_frame.cols,
                                                  bgr_frame.rows);
        obs.decoded = true;
        if (markerDebugEnabled()) {
            std::cout << "[MarkerDetector] selected aruco id=" << obs.marker_id
                      << " normalized_x=" << obs.normalized_x
                      << " area=" << obs.area_ratio
                      << " pose_valid=" << obs.pose_valid;
            if (obs.pose_valid) std::cout << " dist_m=" << obs.distance_m;
            std::cout << std::endl;
        }
        return obs;
    }

    return detectPrintedDockSign(gray,
                                 bgr_frame.cols,
                                 bgr_frame.rows,
                                 target_marker_id_,
                                 marker_size_m_,
                                 intrinsics_,
                                 detector_);
}

}  // namespace doly::autocharge