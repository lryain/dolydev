#include "test_common.h"

#include <getopt.h>

#include <cstdlib>
#include <exception>
#include <iomanip>
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
            default:
                throw std::invalid_argument("invalid arguments");
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        auto log = tof_test::openLogFile("tof_simpletest");
        const tof_test::TofConfig tofConfig = tof_test::loadTofConfig(&log);

        tof_test::SetupOptions setupOptions;
        setupOptions.skipSetup = options.skipSetup;
        setupOptions.forceSetup = options.forceSetup;
        setupOptions.autoConfirm = options.autoConfirm;
        setupOptions.setupMarker = tof_test::paths().setupMarker;
        tof_test::setupSensors(setupOptions, &log);

        const tof_test::OffsetPair offsets = tof_test::loadOffsets(&log);
        std::unique_ptr<tof_test::Sensor> left;
        std::unique_ptr<tof_test::Sensor> right;
        if (options.side == "left" || options.side == "both") {
            left = std::make_unique<tof_test::Sensor>(tof_test::kI2cBus, tof_test::kLeftAddress, offsets.left);
        }
        if (options.side == "right" || options.side == "both") {
            right = std::make_unique<tof_test::Sensor>(tof_test::kI2cBus, tof_test::kRightAddress, offsets.right);
        }

        std::ostringstream header;
        if (left != nullptr && right != nullptr) {
            header << "当前左侧偏移量: " << left->offset() << " mm, 右侧偏移量: " << right->offset() << " mm";
        } else if (left != nullptr) {
            header << "当前左侧偏移量: " << left->offset() << " mm";
        } else if (right != nullptr) {
            header << "当前右侧偏移量: " << right->offset() << " mm";
        }
        tof_test::printLine(header.str(), &log);
        tof_test::printLine("Starting simple sampler. Ctrl-C to stop.", &log);

        int iteration = 0;
        const int delayMs = static_cast<int>(options.intervalSeconds * 1000.0);
        while (!tof_test::limitReached(iteration, options.samples)) {
            std::ostringstream line;
            if (left != nullptr && right != nullptr) {
                line << "L=" << std::setw(4) << left->range() << " mm | R=" << std::setw(4) << right->range()
                     << " mm | Llux=" << left->readLux(tof_test::parseGainValue(tofConfig.alsGainLeft)) << " | Rlux=" << right->readLux(tof_test::parseGainValue(tofConfig.alsGainRight));
            } else if (left != nullptr) {
                line << "L=" << std::setw(4) << left->range() << " mm | Llux=" << left->readLux(tof_test::parseGainValue(tofConfig.alsGainLeft));
            } else if (right != nullptr) {
                line << "R=" << std::setw(4) << right->range() << " mm | Rlux=" << right->readLux(tof_test::parseGainValue(tofConfig.alsGainRight));
            }
            tof_test::printLine(line.str(), &log);
            ++iteration;
            if (!tof_test::limitReached(iteration, options.samples) && delayMs > 0) {
                tof_test::sleepMs(delayMs);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tof_simpletest failed: " << error.what() << std::endl;
        return 1;
    }
}