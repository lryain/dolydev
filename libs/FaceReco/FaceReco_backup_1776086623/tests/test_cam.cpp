#include <iostream>
#include <cstdlib>
#include <opencv2/opencv.hpp>

int main()
{
#ifdef _WIN32
    cv::VideoCapture cap(0);
#else
    // On Linux prefer V4L2 backend
    cv::VideoCapture cap(0, cv::CAP_V4L2);
#endif

    if (!cap.isOpened()) {
        std::cerr << "ERROR: cannot open camera device 0" << std::endl;
        return -1;
    }

    // By default try to open a GUI window. If that fails (no X/GTK), fall back to headless snapshot.
    bool enable_gui = true;
#ifndef _WIN32
    const char *disp = std::getenv("DISPLAY");
    if (disp == nullptr || std::strlen(disp) == 0) {
        // No DISPLAY; we'll still try to create window but be ready to catch exceptions
        std::cout << "[INFO] DISPLAY not set. Will attempt to create window and fallback if it fails." << std::endl;
    }
#endif

    cv::Mat frame;
    std::cout << "Starting capture. Press 'q' in the window to quit (if GUI available)." << std::endl;

    // Try to create window (may throw if GTK backend can't initialize)
    try {
        cv::namedWindow("capture", cv::WINDOW_AUTOSIZE);
    } catch (const cv::Exception &e) {
        std::cerr << "[WARN] Failed to create OpenCV window: " << e.what() << std::endl;
        enable_gui = false;
    }

    while (true) {
        if (!cap.read(frame)) {
            std::cerr << "WARNING: failed to read frame" << std::endl;
            continue;
        }

        if (frame.empty()) {
            std::cerr << "WARNING: empty frame" << std::endl;
            continue;
        }

        if (enable_gui) {
            cv::imshow("capture", frame);
            char c = (char)cv::waitKey(1);
            if (c == 'q') {
                cv::imwrite("takephoto2.jpg", frame);
                std::cout << "take Photo Ok" << std::endl;
                break;
            }
        } else {
            // Headless: save a single snapshot then exit
            cv::imwrite("takephoto2.jpg", frame);
            std::cout << "Headless: saved takephoto2.jpg and exiting." << std::endl;
            break;
        }
    }

    cap.release();
    if (enable_gui) cv::destroyAllWindows();
    return 0;
}
