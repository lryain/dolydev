#include "test_common.h"

#include <getopt.h>
#include <signal.h>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>

namespace {

std::atomic_bool g_stopRequested{false};

void handleExitSignal(int) {
    g_stopRequested = true;
}

struct Options {
    bool skipSetup = false;
    bool forceSetup = false;
    bool autoConfirm = false;
    int historyCount = 1;
    double intervalSeconds = 0.01;
    int samples = 0;
};

Options parseArgs(int argc, char** argv) {
    Options options;
    const option longOptions[] = {
        {"skip-setup", no_argument, nullptr, 's'},
        {"force-setup", no_argument, nullptr, 'f'},
        {"auto-confirm", no_argument, nullptr, 'y'},
        {"count", required_argument, nullptr, 'c'},
        {"interval", required_argument, nullptr, 'i'},
        {"samples", required_argument, nullptr, 'p'},
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
                options.historyCount = std::atoi(optarg);
                break;
            case 'i':
                options.intervalSeconds = std::atof(optarg);
                break;
            case 'p':
                options.samples = std::atoi(optarg);
                break;
            default:
                throw std::invalid_argument("invalid arguments");
        }
    }
    return options;
}

std::string formatHistory(const std::vector<uint8_t>& history) {
    if (history.empty()) return "[]";
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < history.size(); ++i) {
        oss << (int)history[i] << (i == history.size() - 1 ? "" : ", ");
    }
    oss << "]";
    return oss.str();
}

}  // namespace

int main(int argc, char** argv) {
    signal(SIGTERM, handleExitSignal);
    signal(SIGHUP, handleExitSignal);
    signal(SIGINT, handleExitSignal);

    try {
        const Options options = parseArgs(argc, argv);
        auto log = tof_test::openLogFile("tof_historytest");

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

        tof_test::printLine("Starting continuous mode", &log);
        left.startContinuous(20);
        right.startContinuous(20);

        int iteration = 0;
        const int delayMs = static_cast<int>(options.intervalSeconds * 1000.0);
        while (!g_stopRequested && !tof_test::limitReached(iteration, options.samples)) {
            std::vector<uint8_t> histL = left.rangeHistory(options.historyCount);
            std::vector<uint8_t> histR = right.rangeHistory(options.historyCount);
            
            std::ostringstream line;
            line << "Left: " << formatHistory(histL) << " | Right: " << formatHistory(histR);
            tof_test::printLine(line.str(), &log);
            
            ++iteration;
            if (delayMs > 0) tof_test::sleepMs(delayMs);
        }

        tof_test::stopContinuousSafe(&left, "left", &log);
        tof_test::stopContinuousSafe(&right, "right", &log);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tof_historytest failed: " << error.what() << std::endl;
        return 1;
    }
}