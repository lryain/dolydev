#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>

namespace {

void drawRoundedOutline(cv::Mat& canvas, int inset, int radius, int thickness) {
    const int left = inset;
    const int top = inset;
    const int right = canvas.cols - inset;
    const int bottom = canvas.rows - inset;
    const cv::Scalar color(0, 0, 0);

    cv::line(canvas, cv::Point(left + radius, top), cv::Point(right - radius, top), color, thickness, cv::LINE_AA);
    cv::line(canvas, cv::Point(left + radius, bottom), cv::Point(right - radius, bottom), color, thickness, cv::LINE_AA);
    cv::line(canvas, cv::Point(left, top + radius), cv::Point(left, bottom - radius), color, thickness, cv::LINE_AA);
    cv::line(canvas, cv::Point(right, top + radius), cv::Point(right, bottom - radius), color, thickness, cv::LINE_AA);
    cv::ellipse(canvas, cv::Point(left + radius, top + radius), cv::Size(radius, radius), 180.0, 0.0, 90.0, color, thickness, cv::LINE_AA);
    cv::ellipse(canvas, cv::Point(right - radius, top + radius), cv::Size(radius, radius), 270.0, 0.0, 90.0, color, thickness, cv::LINE_AA);
    cv::ellipse(canvas, cv::Point(right - radius, bottom - radius), cv::Size(radius, radius), 0.0, 0.0, 90.0, color, thickness, cv::LINE_AA);
    cv::ellipse(canvas, cv::Point(left + radius, bottom - radius), cv::Size(radius, radius), 90.0, 0.0, 90.0, color, thickness, cv::LINE_AA);
}

void drawGuideRailDockIcon(cv::Mat& canvas, int center_x, int top_y, int width, int height) {
    const cv::Scalar black(0, 0, 0);
    const int center_y = top_y + height / 2;
    const int rail_top = top_y + height / 7;
    const int rail_bottom = top_y + height - height / 5;
    const int rail_offset = width / 3;
    const int rail_span = width / 7;
    const int stroke = std::max(10, width / 18);

    std::vector<cv::Point> left_rail{
        cv::Point(center_x - rail_offset, rail_top + height / 10),
        cv::Point(center_x - rail_span, rail_top),
        cv::Point(center_x - rail_span, rail_bottom),
        cv::Point(center_x - rail_offset, rail_bottom + height / 8),
    };
    std::vector<cv::Point> right_rail{
        cv::Point(center_x + rail_offset, rail_top + height / 10),
        cv::Point(center_x + rail_span, rail_top),
        cv::Point(center_x + rail_span, rail_bottom),
        cv::Point(center_x + rail_offset, rail_bottom + height / 8),
    };

    cv::polylines(canvas, left_rail, true, black, stroke, cv::LINE_AA);
    cv::polylines(canvas, right_rail, true, black, stroke, cv::LINE_AA);
    cv::line(canvas,
             cv::Point(center_x - width / 6, center_y),
             cv::Point(center_x + width / 6, center_y),
             black,
             stroke,
             cv::LINE_AA);
    cv::rectangle(canvas,
                  cv::Point(center_x - width / 16, center_y - height / 8),
                  cv::Point(center_x + width / 16, rail_bottom + height / 9),
                  black,
                  cv::FILLED);
    cv::rectangle(canvas,
                  cv::Point(center_x - width / 3, top_y + height - height / 12),
                  cv::Point(center_x - width / 3 + width / 8, top_y + height - height / 24),
                  black,
                  cv::FILLED);
    cv::rectangle(canvas,
                  cv::Point(center_x + width / 3 - width / 8, top_y + height - height / 12),
                  cv::Point(center_x + width / 3, top_y + height - height / 24),
                  black,
                  cv::FILLED);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <output.png> <marker-id> [marker-pixels]\n";
        return 1;
    }

    const std::string output = argv[1];
    const int marker_id = std::atoi(argv[2]);
    const int marker_pixels = (argc >= 4) ? std::atoi(argv[3]) : 800;
    const int quiet_zone = std::max(80, marker_pixels / 4);
    const int label_height = std::max(180, marker_pixels / 2);
    const int page_width = marker_pixels + quiet_zone * 2;
    const int page_height = marker_pixels + quiet_zone * 2 + label_height;

    cv::Mat marker;
    cv::aruco::generateImageMarker(
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50),
        marker_id,
        marker_pixels,
        marker,
        1);

    cv::Mat printable(page_height, page_width, CV_8UC3, cv::Scalar(255, 255, 255));
    drawRoundedOutline(printable, quiet_zone / 6, quiet_zone / 2, std::max(16, quiet_zone / 8));
    const int marker_x = quiet_zone;
    const int marker_y = quiet_zone;
    cv::Mat marker_region = printable(cv::Rect(marker_x, marker_y, marker_pixels, marker_pixels));
    cv::cvtColor(marker, marker_region, cv::COLOR_GRAY2BGR);

    const int icon_width = marker_pixels / 2;
    const int icon_height = marker_pixels / 4;
    drawGuideRailDockIcon(
        printable,
        page_width / 2,
        marker_y + marker_pixels + quiet_zone / 2,
        icon_width,
        icon_height);

    cv::putText(printable,
                "DOLY CHARGE DOCK",
                cv::Point(page_width / 2 - marker_pixels / 3, page_height - label_height / 5),
                cv::FONT_HERSHEY_SIMPLEX,
                0.95,
                cv::Scalar(0, 0, 0),
                2,
                cv::LINE_AA);
    cv::putText(printable,
                "ARUCO ID " + std::to_string(marker_id),
                cv::Point(page_width / 2 - marker_pixels / 5, page_height - label_height / 12),
                cv::FONT_HERSHEY_SIMPLEX,
                0.75,
                cv::Scalar(0, 0, 0),
                2,
                cv::LINE_AA);

    if (!cv::imwrite(output, printable)) {
        std::cerr << "Failed to write marker to " << output << std::endl;
        return 2;
    }

    std::cout << "Marker generated: " << output
              << " (marker=" << marker_pixels << "px, quiet-zone=" << quiet_zone << "px)"
              << std::endl;
    return 0;
}