#include "autocharge/debug_renderer.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <iomanip>
#include <algorithm>
#include <sstream>

namespace doly::autocharge {

namespace {

void putOutlinedText(cv::Mat& image,
                     const std::string& text,
                     cv::Point origin,
                     double scale,
                     const cv::Scalar& color,
                     int thickness = 2,
                     int outline = 4) {
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0, 0, 0), outline, cv::LINE_AA);
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness, cv::LINE_AA);
}

cv::Rect buildPreviewRoi(const cv::Mat& frame, const MarkerObservation& observation) {
    if (!observation.found || observation.corners.empty()) {
        return cv::Rect(0, 0, frame.cols, frame.rows);
    }

    cv::Rect bbox = cv::boundingRect(observation.corners);
    const int pad_x = std::max(30, bbox.width / 2);
    const int pad_y = std::max(24, bbox.height / 2);
    bbox.x = std::max(0, bbox.x - pad_x);
    bbox.y = std::max(0, bbox.y - pad_y);
    bbox.width = std::min(frame.cols - bbox.x, bbox.width + pad_x * 2);
    bbox.height = std::min(frame.rows - bbox.y, bbox.height + pad_y * 2);
    return bbox;
}

std::string formatAction(const MotionCommand& command) {
    std::ostringstream stream;
    switch (command.type) {
        case MotionType::Rotate:
            stream << "TURN " << std::fixed << std::setprecision(1) << command.rotate_deg << "d";
            break;
        case MotionType::Forward:
            stream << "FWD " << std::fixed << std::setprecision(0) << command.forward_mm << "mm";
            break;
        case MotionType::Backward:
            stream << "REV " << std::fixed << std::setprecision(0) << command.forward_mm << "mm";
            break;
        case MotionType::Stop:
            stream << "STOP";
            break;
        case MotionType::None:
            stream << "HOLD";
            break;
    }
    return stream.str();
}

std::string formatTof(const TofStatus& tof_status) {
    std::ostringstream stream;
    stream << "TOF ";
    if (!tof_status.valid) {
        stream << "--/--";
        return stream.str();
    }
    stream << (tof_status.left_valid ? std::to_string(tof_status.left_mm) : "--")
           << "/"
           << (tof_status.right_valid ? std::to_string(tof_status.right_mm) : "--");
    return stream.str();
}

}  // namespace

DebugRenderer::DebugRenderer(DebugViewConfig config)
    : config_(std::move(config)) {}

cv::Mat DebugRenderer::render(const cv::Mat& frame,
                              const MarkerObservation& observation,
                              const MotionCommand& command,
                              const PowerStatus& power_status,
                              const TofStatus& tof_status) const {
    const int canvas_size = std::max(120, config_.canvas_size);
    const int preview_height = std::clamp(config_.preview_height, 0, canvas_size);
    const int panel_top = std::clamp(config_.panel_top, 0, canvas_size);
    cv::Mat output(canvas_size, canvas_size, CV_8UC3, config_.background_color);

    cv::Mat annotated;
    if (frame.empty()) {
        annotated = cv::Mat(canvas_size, canvas_size, CV_8UC3, cv::Scalar(45, 45, 45));
    } else {
        annotated = frame.clone();
    }

    if (config_.marker_box.enabled && observation.found && observation.corners.size() == 4) {
        std::vector<cv::Point> marker_poly;
        marker_poly.reserve(observation.corners.size());
        for (const auto& corner : observation.corners) {
            marker_poly.emplace_back(static_cast<int>(corner.x), static_cast<int>(corner.y));
        }
        cv::polylines(annotated, marker_poly, true, config_.marker_box.color, config_.marker_box.thickness, cv::LINE_AA);

        if (config_.marker_center.enabled) {
            cv::Point center(0, 0);
            for (const auto& point : observation.corners) {
                center.x += static_cast<int>(point.x);
                center.y += static_cast<int>(point.y);
            }
            center.x /= 4;
            center.y /= 4;
            cv::circle(annotated, center, std::max(1, config_.marker_center.radius), config_.marker_center.color, cv::FILLED, cv::LINE_AA);
        }
    }

    if (config_.center_line.enabled) {
        cv::line(annotated,
                 cv::Point(annotated.cols / 2, 0),
                 cv::Point(annotated.cols / 2, annotated.rows - 1),
                 config_.center_line.color,
                 config_.center_line.thickness,
                 cv::LINE_AA);
    }

    if (config_.preview_enabled && preview_height > 0) {
        const cv::Rect preview_roi = buildPreviewRoi(annotated, observation);
        cv::Mat preview;
        cv::resize(annotated(preview_roi), preview, cv::Size(canvas_size, preview_height), 0.0, 0.0, cv::INTER_AREA);
        preview.copyTo(output(cv::Rect(0, 0, canvas_size, preview_height)));
    }

    if (panel_top < canvas_size) {
        cv::rectangle(output, cv::Rect(0, panel_top, canvas_size, canvas_size - panel_top), config_.panel_color, cv::FILLED);
        cv::rectangle(output, cv::Rect(0, panel_top, canvas_size, std::min(28, canvas_size - panel_top)), config_.header_color, cv::FILLED);
    }

    if (config_.phase.enabled) {
        putOutlinedText(output,
                        toString(command.phase),
                        cv::Point(config_.phase.x, config_.phase.y),
                        config_.phase.scale,
                        config_.phase.color,
                        config_.phase.thickness,
                        config_.phase.outline);
    }

    if (config_.action.enabled) {
        cv::Scalar action_color = config_.action.color;
        if (command.type == MotionType::Stop) {
            action_color = cv::Scalar(80, 255, 120);
        }
        putOutlinedText(output,
                        formatAction(command),
                        cv::Point(config_.action.x, config_.action.y),
                        config_.action.scale,
                        action_color,
                        config_.action.thickness,
                        config_.action.outline);
    }

    std::ostringstream line_metrics;
    line_metrics << std::fixed << std::setprecision(2)
                 << "X " << observation.normalized_x
                 << "  D " << std::max(-1.0, observation.distance_m);
    if (config_.metrics.enabled) {
        putOutlinedText(output,
                        line_metrics.str(),
                        cv::Point(config_.metrics.x, config_.metrics.y),
                        config_.metrics.scale,
                        config_.metrics.color,
                        config_.metrics.thickness,
                        config_.metrics.outline);
    }

    std::ostringstream line_power;
    line_power << std::fixed << std::setprecision(2)
               << "V " << power_status.voltage_v
               << "  I " << std::setprecision(3) << power_status.current_a;
    if (config_.power.enabled) {
        const cv::Scalar power_color = power_status.is_charging ? cv::Scalar(80, 255, 120) : config_.power.color;
        putOutlinedText(output,
                        line_power.str(),
                        cv::Point(config_.power.x, config_.power.y),
                        config_.power.scale,
                        power_color,
                        config_.power.thickness,
                        config_.power.outline);
    }

    if (config_.tof.enabled) {
        putOutlinedText(output,
                        formatTof(tof_status),
                        cv::Point(config_.tof.x, config_.tof.y),
                        config_.tof.scale,
                        config_.tof.color,
                        config_.tof.thickness,
                        config_.tof.outline);
    }

    const std::string reason = command.reason.size() > 22 ? command.reason.substr(0, 22) : command.reason;
    if (config_.reason.enabled) {
        putOutlinedText(output,
                        reason,
                        cv::Point(config_.reason.x, config_.reason.y),
                        config_.reason.scale,
                        config_.reason.color,
                        config_.reason.thickness,
                        config_.reason.outline);
    }
    return output;
}

}  // namespace doly::autocharge