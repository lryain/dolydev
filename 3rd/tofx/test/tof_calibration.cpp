#include "test_common.h"

#include <getopt.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>

namespace {

struct Options {
    bool skipSetup = false;
    bool forceSetup = false;
    bool autoConfirm = false;
    bool forceCalibration = false;
    bool nonInteractive = false;
    double leftTargetMm = -1.0;
    double rightTargetMm = -1.0;
    int samples = -1;
    double intervalSeconds = -1.0;
};

Options parseArgs(int argc, char** argv) {
    Options options;
    const option longOptions[] = {
        {"skip-setup", no_argument, nullptr, 's'},
        {"force-setup", no_argument, nullptr, 'f'},
        {"auto-confirm", no_argument, nullptr, 'y'},
        {"force-calibration", no_argument, nullptr, 'c'},
        {"non-interactive", no_argument, nullptr, 'n'},
        {"left-target-mm", required_argument, nullptr, 'l'},
        {"right-target-mm", required_argument, nullptr, 'r'},
        {"samples", required_argument, nullptr, 'm'},
        {"interval", required_argument, nullptr, 'i'},
        {nullptr, 0, nullptr, 0},
    };

    while (true) {
        const int current = getopt_long(argc, argv, "", longOptions, nullptr);
        if (current == -1) {
            break;
        }
        switch (current) {
            case 's':
                options.skipSetup = true;
                break;
            case 'f':
                options.forceSetup = true;
                break;
            case 'y':
                options.autoConfirm = true;
                break;
            case 'c':
                options.forceCalibration = true;
                break;
            case 'n':
                options.nonInteractive = true;
                break;
            case 'l':
                options.leftTargetMm = std::atof(optarg);
                break;
            case 'r':
                options.rightTargetMm = std::atof(optarg);
                break;
            case 'm':
                options.samples = std::atoi(optarg);
                break;
            case 'i':
                options.intervalSeconds = std::atof(optarg);
                break;
            default:
                throw std::invalid_argument("invalid arguments");
        }
    }
    return options;
}

double collectAverage(tof_test::Sensor& sensor, const std::string& label, int samples, int delayMs, std::ofstream* log) {
    sensor.setOffset(0);
    double total = 0.0;
    for (int index = 0; index < samples; ++index) {
        total += static_cast<double>(sensor.range());
        if (index + 1 < samples && delayMs > 0) {
            tof_test::sleepMs(delayMs);
        }
    }
    const double average = total / static_cast<double>(samples);
    std::ostringstream line;
    line << label << " average over " << samples << " samples: " << average << " mm";
    tof_test::printLine(line.str(), log);
    return average;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = parseArgs(argc, argv);
        auto log = tof_test::openLogFile("tof_calibration");

        // Use values from config unless overridden by command-line
        auto config = tof_test::loadTofConfig(&log);
        if (options.leftTargetMm < 0) options.leftTargetMm = config.calTargetMmLeft;
        if (options.rightTargetMm < 0) options.rightTargetMm = config.calTargetMmRight;
        if (options.samples < 0) options.samples = config.calSamples;
        if (options.intervalSeconds < 0) options.intervalSeconds = config.calIntervalSeconds;

        tof_test::SetupOptions setupOptions;
        setupOptions.skipSetup = options.skipSetup;
        setupOptions.forceSetup = options.forceSetup;
        setupOptions.autoConfirm = options.autoConfirm;
        setupOptions.setupMarker = tof_test::paths().setupMarker;
        tof_test::setupSensors(setupOptions, &log);

        tof_test::Sensor left(tof_test::kI2cBus, tof_test::kLeftAddress);
        tof_test::Sensor right(tof_test::kI2cBus, tof_test::kRightAddress);

        const tof_test::OffsetPair savedOffsets = tof_test::loadOffsets(&log);
        left.setOffset(savedOffsets.left);
        right.setOffset(savedOffsets.right);

        if (std::filesystem::exists(tof_test::paths().calibrationMarker) && !options.forceCalibration) {
            tof_test::printLine(
                "calibrate_sensors: calibration marker exists; skipping calibration",
                &log);
            return 0;
        }

        std::ostringstream currentOffsets;
        currentOffsets << "当前左侧偏移量: " << left.offset() << " mm, 右侧偏移量: " << right.offset() << " mm";
        tof_test::printLine(currentOffsets.str(), &log);

        if (!options.nonInteractive) {
            std::ostringstream leftPrompt;
            leftPrompt << "请将左侧传感器前方放置一个距离 " << options.leftTargetMm << " mm 的目标物，按回车继续...";
            tof_test::printLine(leftPrompt.str(), &log);
            std::string ignored;
            std::getline(std::cin, ignored);
        }
        const int delayMs = static_cast<int>(options.intervalSeconds * 1000.0);
        const double avgLeft = collectAverage(left, "左侧", options.samples, delayMs, &log);
        const int leftOffset = tof_test::clampOffset(options.leftTargetMm - avgLeft);
        left.setOffset(leftOffset);
        tof_test::printLine("Left TOF calibration offset to apply: " + std::to_string(leftOffset) + " mm", &log);

        if (!options.nonInteractive) {
            std::ostringstream rightPrompt;
            rightPrompt << "请将右侧传感器前方放置一个距离 " << options.rightTargetMm << " mm 的目标物，按回车继续...";
            tof_test::printLine(rightPrompt.str(), &log);
            std::string ignored;
            std::getline(std::cin, ignored);
        }
        const double avgRight = collectAverage(right, "右侧", options.samples, delayMs, &log);
        const int rightOffset = tof_test::clampOffset(options.rightTargetMm - avgRight);
        right.setOffset(rightOffset);
        tof_test::printLine("Right TOF calibration offset to apply: " + std::to_string(rightOffset) + " mm", &log);

        tof_test::saveOffsets({leftOffset, rightOffset}, &log);
        std::ofstream marker(tof_test::paths().calibrationMarker, std::ios::trunc);
        marker << std::time(nullptr) << " left=" << leftOffset << " right=" << rightOffset << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tof_calibration failed: " << error.what() << std::endl;
        return 1;
    }
}