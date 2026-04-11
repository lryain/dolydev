#include "test_common.h"

#include <getopt.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>

namespace {

struct Options {
    bool skipSetup = false;
    bool forceSetup = false;
    bool autoConfirm = false;
    int measurements = 100;
};

Options parseArgs(int argc, char** argv) {
    Options options;
    const option longOptions[] = {
        {"skip-setup", no_argument, nullptr, 's'},
        {"force-setup", no_argument, nullptr, 'f'},
        {"auto-confirm", no_argument, nullptr, 'y'},
        {"measurements", required_argument, nullptr, 'm'},
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
            case 'm':
                options.measurements = std::atoi(optarg);
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
        auto log = tof_test::openLogFile("tof_perform");

        tof_test::SetupOptions setupOptions;
        setupOptions.skipSetup = options.skipSetup;
        setupOptions.forceSetup = options.forceSetup;
        setupOptions.autoConfirm = options.autoConfirm;
        setupOptions.setupMarker = tof_test::paths().setupMarker;
        tof_test::setupSensors(setupOptions, &log);

        const tof_test::OffsetPair offsets = tof_test::loadOffsets(&log);
        tof_test::Sensor left(tof_test::kI2cBus, tof_test::kLeftAddress, offsets.left);
        tof_test::Sensor right(tof_test::kI2cBus, tof_test::kRightAddress, offsets.right);

        std::ostringstream currentOffsets;
        currentOffsets << "当前左侧偏移量: " << left.offset() << " mm, 右侧偏移量: " << right.offset() << " mm";
        tof_test::printLine(currentOffsets.str(), &log);

        // Phase 1: Single-shot
        tof_test::printLine("Starting single-shot performance measurement...", &log);
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < options.measurements; ++i) {
            left.range();
            right.range();
        }
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> diff = end - start;
        std::ostringstream ss_res;
        ss_res << "Performed " << options.measurements << " measurements in single-shot mode in " << diff.count() << "s\n";
        tof_test::printLine(ss_res.str(), &log);

        tof_test::sleepMs(2000);

        // Phase 2: Continuous
        tof_test::printLine("Starting continuous performance measurement...", &log);
        left.startContinuous(20);
        right.startContinuous(20);
        start = std::chrono::steady_clock::now();
        for (int i = 0; i < options.measurements; ++i) {
            left.rangeContinuous(200);
            right.rangeContinuous(200);
        }
        end = std::chrono::steady_clock::now();
        diff = end - start;
        std::ostringstream cont_res;
        cont_res << "Performed " << options.measurements << " measurements in continuous mode in " << diff.count() << "s\n";
        tof_test::printLine(cont_res.str(), &log);

        // Phase 3: History (continuous already started)
        tof_test::printLine("Starting continuous measurement with history enabled performance measurement...", &log);
        start = std::chrono::steady_clock::now();
        for (int i = 0; i < options.measurements; ++i) {
            left.rangeFromHistory();
            right.rangeFromHistory();
        }
        end = std::chrono::steady_clock::now();
        diff = end - start;
        std::ostringstream hist_res;
        hist_res << "Performed " << options.measurements << " measurements in continuous mode, reading from history, in " << diff.count() << "s\n";
        tof_test::printLine(hist_res.str(), &log);

        tof_test::stopContinuousSafe(&left, "left", &log);
        tof_test::stopContinuousSafe(&right, "right", &log);
        tof_test::printLine("Finished", &log);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tof_perform failed: " << error.what() << std::endl;
        return 1;
    }
}