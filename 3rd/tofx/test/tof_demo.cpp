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
    std::string side = "both";
    double intervalSeconds = 0.05;
    int samples = 0;
    bool leftOffsetOverrideEnabled = false;
    bool rightOffsetOverrideEnabled = false;
    int leftOffsetOverride = 0;
    int rightOffsetOverride = 0;
};

Options parseArgs(int argc, char** argv) {
    Options options;
    const option longOptions[] = {
        {"skip-setup", no_argument, nullptr, 's'},
        {"force-setup", no_argument, nullptr, 'f'},
        {"auto-confirm", no_argument, nullptr, 'y'},
        {"side", required_argument, nullptr, 'd'},
        {"interval", required_argument, nullptr, 'i'},
        {"samples", required_argument, nullptr, 'n'},
        {"left-offset", required_argument, nullptr, 'l'},
        {"right-offset", required_argument, nullptr, 'r'},
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
            case 'd':
                options.side = optarg;
                break;
            case 'i':
                options.intervalSeconds = std::atof(optarg);
                break;
            case 'n':
                options.samples = std::atoi(optarg);
                break;
            case 'l':
                options.leftOffsetOverrideEnabled = true;
                options.leftOffsetOverride = std::atoi(optarg);
                break;
            case 'r':
                options.rightOffsetOverrideEnabled = true;
                options.rightOffsetOverride = std::atoi(optarg);
                break;
            default:
                throw std::invalid_argument("invalid arguments");
        }
    }

    if (options.side != "left" && options.side != "right" && options.side != "both") {
        throw std::invalid_argument("--side must be left, right or both");
    }
    if (options.intervalSeconds < 0.0) {
        throw std::invalid_argument("--interval must be non-negative");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        auto log = tof_test::openLogFile("tof_demo");
        const tof_test::TofConfig tofConfig = tof_test::loadTofConfig(&log);

        tof_test::SetupOptions setupOptions;
        setupOptions.skipSetup = options.skipSetup;
        setupOptions.forceSetup = options.forceSetup;
        setupOptions.autoConfirm = options.autoConfirm;
        setupOptions.setupMarker = tof_test::paths().setupMarker;
        tof_test::setupSensors(setupOptions, &log);

        tof_test::OffsetPair offsets = tof_test::loadOffsets(&log);
        if (options.leftOffsetOverrideEnabled) {
            offsets.left = options.leftOffsetOverride;
        }
        if (options.rightOffsetOverrideEnabled) {
            offsets.right = options.rightOffsetOverride;
        }
        std::unique_ptr<tof_test::Sensor> left;
        std::unique_ptr<tof_test::Sensor> right;

        if (options.side == "left" || options.side == "both") {
            left = std::make_unique<tof_test::Sensor>(tof_test::kI2cBus, tof_test::kLeftAddress, offsets.left);
        }
        if (options.side == "right" || options.side == "both") {
            right = std::make_unique<tof_test::Sensor>(tof_test::kI2cBus, tof_test::kRightAddress, offsets.right);
        }

        if (left != nullptr && right != nullptr) {
            std::ostringstream message;
            message << "当前左侧偏移量: " << left->offset() << " mm, 右侧偏移量: " << right->offset() << " mm";
            tof_test::printLine(message.str(), &log);
        } else if (left != nullptr) {
            tof_test::printLine("当前左侧偏移量: " + std::to_string(left->offset()) + " mm", &log);
        } else if (right != nullptr) {
            tof_test::printLine("当前右侧偏移量: " + std::to_string(right->offset()) + " mm", &log);
        }

        tof_test::printLine("TOF demo running. Ctrl-C to exit.", &log);
        int iteration = 0;
        const int delayMs = static_cast<int>(options.intervalSeconds * 1000.0);

        while (!tof_test::limitReached(iteration, options.samples)) {
            std::ostringstream line;
            if (left != nullptr && right != nullptr) {
                line << "L=" << std::setw(4) << left->range() 
                     << " mm | R=" << std::setw(4) << right->range() << " mm";
            } else if (left != nullptr) {
                line << "L=" << std::setw(4) << left->range() << " mm";
            } else if (right != nullptr) {
                line << "R=" << std::setw(4) << right->range() << " mm";
            }
            tof_test::printLine(line.str(), &log);
            ++iteration;
            if (!tof_test::limitReached(iteration, options.samples) && delayMs > 0) {
                tof_test::sleepMs(delayMs);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tof_demo failed: " << error.what() << std::endl;
        return 1;
    }
}