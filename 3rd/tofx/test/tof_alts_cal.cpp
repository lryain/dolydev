#include "test_common.h"

#include <getopt.h>
#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

std::atomic_bool g_stopRequested{false};

void handleExitSignal(int) {
    g_stopRequested = true;
}

struct Options {
    bool skipSetup = false;
    bool forceSetup = false;
    bool autoConfirm = false;
    bool forceCalibration = false;
    bool skipCalibration = false;
    bool nonInteractive = false;
    double intervalSeconds = 1.0;
    int gainLeft = 7;
    int gainRight = 7;
    int calibrationSamples = 32;
    double calibrationIntervalSeconds = 0.05;
    int samples = 0;
};

bool timedReadLux(
    tof_test::Sensor& sensor,
    vl6180x_als_gain_t gain,
    std::chrono::milliseconds timeout,
    double& value,
    std::string& error);

Options parseArgs(int argc, char** argv, const tof_test::TofConfig& defaults) {
    Options options;
    options.intervalSeconds = defaults.alsCalIntervalSeconds;
    options.gainLeft = defaults.alsCalGainLeft;
    options.gainRight = defaults.alsCalGainRight;
    options.calibrationSamples = defaults.alsCalSamples;
    options.calibrationIntervalSeconds = defaults.alsCalIntervalSecondsFast;
    const option longOptions[] = {
        {"skip-setup", no_argument, nullptr, 's'},
        {"force-setup", no_argument, nullptr, 'f'},
        {"auto-confirm", no_argument, nullptr, 'y'},
        {"force-cali-lux", no_argument, nullptr, 'c'},
        {"skip-calibration", no_argument, nullptr, 'k'},
        {"non-interactive", no_argument, nullptr, 'n'},
        {"interval", required_argument, nullptr, 'i'},
        {"gain-left", required_argument, nullptr, 'l'},
        {"gain-right", required_argument, nullptr, 'r'},
        {"cali-lux-samples", required_argument, nullptr, 'm'},
        {"cali-lux-interval", required_argument, nullptr, 't'},
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
                options.forceCalibration = true;
                break;
            case 'k':
                options.skipCalibration = true;
                break;
            case 'n':
                options.nonInteractive = true;
                break;
            case 'i':
                options.intervalSeconds = std::atof(optarg);
                break;
            case 'l':
                options.gainLeft = std::atoi(optarg);
                break;
            case 'r':
                options.gainRight = std::atoi(optarg);
                break;
            case 'm':
                options.calibrationSamples = std::atoi(optarg);
                break;
            case 't':
                options.calibrationIntervalSeconds = std::atof(optarg);
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

double collectAverage(tof_test::Sensor& sensor, vl6180x_als_gain_t gain, int samples, int delayMs) {
    double total = 0.0;
    int collected = 0;
    for (int index = 0; index < samples; ++index) {
        double value = 0.0;
        std::string error;
        if (timedReadLux(sensor, gain, std::chrono::milliseconds(std::max(delayMs * 2, 500)), value, error)) {
            total += value;
            ++collected;
        }
        if (index + 1 < samples && delayMs > 0) {
            tof_test::sleepMs(delayMs);
        }
    }
    if (collected == 0) {
        return -1.0;
    }
    return total / static_cast<double>(collected);
}

bool timedReadLux(
    tof_test::Sensor& sensor,
    vl6180x_als_gain_t gain,
    std::chrono::milliseconds timeout,
    double& value,
    std::string& error) {
    std::promise<double> promise;
    std::future<double> future = promise.get_future();
    std::thread([&sensor, gain, promise = std::move(promise)]() mutable {
        try {
            promise.set_value(static_cast<double>(sensor.readLux(gain)));
        } catch (...) {
            try {
                promise.set_exception(std::current_exception());
            } catch (...) {
            }
        }
    }).detach();

    if (future.wait_for(timeout) != std::future_status::ready) {
        error = "TIMEOUT";
        value = 0.0;
        return false;
    }

    try {
        value = future.get();
        error.clear();
        return true;
    } catch (const std::exception& caught) {
        error = caught.what();
        value = 0.0;
        return false;
    } catch (...) {
        error = "unknown error";
        value = 0.0;
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    signal(SIGTERM, handleExitSignal);
    signal(SIGHUP, handleExitSignal);
    signal(SIGINT, handleExitSignal);

    try {
        auto log = tof_test::openLogFile("tof_alts_cal");
        const tof_test::TofConfig tofConfig = tof_test::loadTofConfig(&log);

        Options options = parseArgs(argc, argv, tofConfig);

        tof_test::SetupOptions setupOptions;
        setupOptions.skipSetup = options.skipSetup;
        setupOptions.forceSetup = options.forceSetup;
        setupOptions.autoConfirm = options.autoConfirm;
        setupOptions.setupMarker = tof_test::paths().setupMarker;
        tof_test::setupSensors(setupOptions, &log);

        tof_test::Sensor left(tof_test::kI2cBus, tof_test::kLeftAddress);
        tof_test::Sensor right(tof_test::kI2cBus, tof_test::kRightAddress);
        const vl6180x_als_gain_t gainLeft = tof_test::parseGainValue(options.gainLeft);
        const vl6180x_als_gain_t gainRight = tof_test::parseGainValue(options.gainRight);

        tof_test::LuxCalibration calibration = tof_test::loadLuxCalibration(&log);
        const bool shouldCalibrate = !options.skipCalibration && (options.forceCalibration || !std::filesystem::exists(tof_test::paths().luxCalibrationMarker));
        if (shouldCalibrate) {
            if (!options.nonInteractive) {
                tof_test::printLine("请将左右 TOF 置于同一光照环境，按回车开始光照校准...", &log);
                std::string ignored;
                std::getline(std::cin, ignored);
            }

            const int delayMs = static_cast<int>(options.calibrationIntervalSeconds * 1000.0);
            const double leftRaw = collectAverage(left, gainLeft, options.calibrationSamples, delayMs);
            const double rightRaw = collectAverage(right, gainRight, options.calibrationSamples, delayMs);
            if (leftRaw > 0.0 && rightRaw > 0.0) {
                const double target = (leftRaw + rightRaw) / 2.0;
                calibration.leftRaw = leftRaw;
                calibration.rightRaw = rightRaw;
                calibration.leftScale = target / leftRaw;
                calibration.rightScale = target / rightRaw;
                tof_test::saveLuxCalibration(calibration, &log);
                std::ofstream marker(tof_test::paths().luxCalibrationMarker, std::ios::trunc);
                marker << std::time(nullptr) << '\n';
                std::ostringstream line;
                line << "lux calibration done: left_raw=" << leftRaw << ", right_raw=" << rightRaw
                     << ", left_scale=" << calibration.leftScale << ", right_scale=" << calibration.rightScale;
                tof_test::printLine(line.str(), &log);
            } else {
                tof_test::printLine("lux calibration failed, fallback to scale=1.0", &log, true);
                calibration = {};
            }
        } else {
            tof_test::printLine("lux calibration marker exists; using saved scales", &log);
        }

        std::ostringstream header;
        header << "Starting continuous ALS read. Left gain=" << options.gainLeft << ", Right gain=" << options.gainRight
               << ", left_scale=" << std::fixed << std::setprecision(4) << calibration.leftScale
               << ", right_scale=" << calibration.rightScale;
        tof_test::printLine(header.str(), &log);

        int iteration = 0;
        const int delayMs = static_cast<int>(options.intervalSeconds * 1000.0);
        bool leftAvailable = true;
        bool rightAvailable = true;
        while (!g_stopRequested && !tof_test::limitReached(iteration, options.samples)) {
            double rawLeft = 0.0;
            double rawRight = 0.0;
            std::string leftError;
            std::string rightError;
            const bool okLeft = leftAvailable && timedReadLux(left, gainLeft, std::chrono::milliseconds(std::max(delayMs * 2, 500)), rawLeft, leftError);
            const bool okRight = rightAvailable && timedReadLux(right, gainRight, std::chrono::milliseconds(std::max(delayMs * 2, 500)), rawRight, rightError);
            if (!okLeft && leftError == "TIMEOUT") {
                leftAvailable = false;
            }
            if (!okRight && rightError == "TIMEOUT") {
                rightAvailable = false;
            }
            std::ostringstream line;
            line << tof_test::nowString() << " | Left: ";
            if (okLeft) {
                line << std::fixed << std::setprecision(2) << (rawLeft * calibration.leftScale) << " lux";
            } else {
                line << (leftAvailable ? "ERR" : "TIMEOUT");
            }
            line << " | Right: ";
            if (okRight) {
                line << std::fixed << std::setprecision(2) << (rawRight * calibration.rightScale) << " lux";
            } else {
                line << (rightAvailable ? "ERR" : "TIMEOUT");
            }
            tof_test::printLine(line.str(), &log);
            ++iteration;
            if (!g_stopRequested && !tof_test::limitReached(iteration, options.samples) && delayMs > 0) {
                tof_test::sleepMs(delayMs);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tof_alts_cal failed: " << error.what() << std::endl;
        return 1;
    }
}