#include "test_common.h"

#include <getopt.h>
#include <signal.h>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>

#include "DriveControl.h"

namespace {

std::atomic_bool g_stopRequested{false};

void handleExitSignal(int) {
    g_stopRequested = true;
}

struct Options {
    bool skipSetup = false;
    bool forceSetup = false;
    bool autoConfirm = false;
    bool dryRun = false;
    bool reverseDirection = false;
    double durationSeconds = 0.0;
};

Options parseArgs(int argc, char** argv) {
    Options options;
    const option longOptions[] = {
        {"skip-setup", no_argument, nullptr, 's'},
        {"force-setup", no_argument, nullptr, 'f'},
        {"auto-confirm", no_argument, nullptr, 'y'},
        {"dry-run", no_argument, nullptr, 'd'},
        {"motor-direction", required_argument, nullptr, 'm'},
        {"duration", required_argument, nullptr, 't'},
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
                options.dryRun = true;
                break;
            case 'm':
                options.reverseDirection = std::string(optarg) == "reverse";
                break;
            case 't':
                options.durationSeconds = std::atof(optarg);
                break;
            default:
                throw std::invalid_argument("invalid arguments");
        }
    }
    return options;
}

void applyWheelCommand(bool isLeft, int signedPercent, bool reverseDirection) {
    const int speed = tof_test::clampPercent(std::abs(signedPercent));
    bool forward = signedPercent >= 0;
    if (reverseDirection) {
        forward = !forward;
    }
    DriveControl::freeDrive(static_cast<uint8_t>(speed), isLeft, forward);
}

void applyBackwardCommand(int percent, bool reverseDirection, bool dryRun, std::ofstream* log) {
    std::ostringstream line;
    line << "backward command speed=" << percent << (dryRun ? " [dry-run]" : "");
    tof_test::printLine(line.str(), log);
    if (dryRun) return;
    applyWheelCommand(true, -percent, reverseDirection);
    applyWheelCommand(false, -percent, reverseDirection);
}

void applyDriveCommand(int leftPercent, int rightPercent, bool reverseDirection, bool dryRun, std::ofstream* log) {
    std::ostringstream line;
    line << "drive command left=" << leftPercent << " right=" << rightPercent
         << (dryRun ? " [dry-run]" : "");
    tof_test::printLine(line.str(), log);
    if (dryRun) {
        return;
    }
    applyWheelCommand(true, leftPercent, reverseDirection);
    applyWheelCommand(false, rightPercent, reverseDirection);
}

void stopDrive(bool dryRun, std::ofstream* log) {
    tof_test::printLine(std::string("stop drive") + (dryRun ? " [dry-run]" : ""), log);
    if (dryRun) {
        return;
    }
    DriveControl::Abort();
    DriveControl::freeDrive(0, true, true);
    DriveControl::freeDrive(0, false, true);
}

}  // namespace

int main(int argc, char** argv) {
    signal(SIGTERM, handleExitSignal);
    signal(SIGHUP, handleExitSignal);
    signal(SIGINT, handleExitSignal);

    try {
        const Options options = parseArgs(argc, argv);
        auto log = tof_test::openLogFile("tof_cruise");
        const tof_test::TofConfig tofConfig = tof_test::loadTofConfig(&log);
        const tof_test::CruiseConfig config = tof_test::loadCruiseConfig(&log);

        tof_test::SetupOptions setupOptions;
        setupOptions.skipSetup = options.skipSetup;
        setupOptions.forceSetup = options.forceSetup;
        setupOptions.autoConfirm = options.autoConfirm;
        setupOptions.setupMarker = tof_test::paths().setupMarker;
        tof_test::setupSensors(setupOptions, &log);

        const tof_test::OffsetPair offsets = tof_test::loadOffsets(&log);
        tof_test::Sensor left(tof_test::kI2cBus, tof_test::kLeftAddress, offsets.left);
        tof_test::Sensor right(tof_test::kI2cBus, tof_test::kRightAddress, offsets.right);

        if (!options.dryRun) {
            const int stopServiceStatus = Helper::stopDolyService();
            tof_test::printLine("Helper::stopDolyService status=" + std::to_string(stopServiceStatus), &log);
            const int driveStatus = DriveControl::init();
            if (driveStatus != 0 && driveStatus != 1) {
                throw std::runtime_error("DriveControl::init failed with status " + std::to_string(driveStatus));
            }
        }

        const auto startTime = std::chrono::steady_clock::now();
        tof_test::printLine("Cruising... Ctrl-C to stop", &log);
        while (!g_stopRequested) {
            if (options.durationSeconds > 0.0) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime);
                if (elapsed.count() >= static_cast<long long>(options.durationSeconds * 1000.0)) {
                    break;
                }
            }

            int distanceLeft = 9999;
            int distanceRight = 9999;
            try {
                distanceLeft = left.range();
            } catch (const std::exception& error) {
                tof_test::printLine(std::string("read left range failed: ") + error.what(), &log, true);
            }
            try {
                distanceRight = right.range();
            } catch (const std::exception& error) {
                tof_test::printLine(std::string("read right range failed: ") + error.what(), &log, true);
            }
            std::ostringstream line;
            line << "L=" << distanceLeft << " R=" << distanceRight;
            tof_test::printLine(line.str(), &log);

            if (distanceLeft <= config.obstacleMm || distanceRight <= config.obstacleMm) {
                const int diff = std::abs(distanceLeft - distanceRight);
                if (diff < config.diffThresholdMm) {
                    // Obstacle ahead symmetrically or boxed in -> Backward
                    applyBackwardCommand(config.backwardSpeedPercent, options.reverseDirection, options.dryRun, &log);
                    tof_test::sleepMs(config.backwardDurationMs);
                } else if (distanceLeft < distanceRight) {
                    // Closer on left -> Turn right
                    applyDriveCommand(config.turnSpeedPercent, -config.turnSpeedPercent, options.reverseDirection, options.dryRun, &log);
                    tof_test::sleepMs(config.turnDurationMs);
                } else {
                    // Closer on right -> Turn left
                    applyDriveCommand(-config.turnSpeedPercent, config.turnSpeedPercent, options.reverseDirection, options.dryRun, &log);
                    tof_test::sleepMs(config.turnDurationMs);
                }
            } else {
                applyDriveCommand(config.forwardSpeedPercent, config.forwardSpeedPercent, options.reverseDirection, options.dryRun, &log);
            }
            tof_test::sleepMs(config.loopDelayMs);
        }

        stopDrive(options.dryRun, &log);
        if (!options.dryRun) {
            DriveControl::dispose(true);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tof_cruise failed: " << error.what() << std::endl;
        return 1;
    }
}