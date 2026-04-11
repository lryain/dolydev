#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "vl6180_pi.h"

namespace tof_test {

namespace fs = std::filesystem;

constexpr int kI2cBus = 6;
constexpr int kLeftAddress = 0x29;
constexpr int kRightAddress = 0x30;

struct OffsetPair {
    int left = 0;
    int right = 0;
};

struct LuxCalibration {
    double leftScale = 1.0;
    double rightScale = 1.0;
    double leftRaw = 0.0;
    double rightRaw = 0.0;
};

struct CruiseConfig {
    int obstacleMm = 120;
    int diffThresholdMm = 40;
    int forwardSpeedPercent = 40;
    int turnSpeedPercent = 30;
    int backwardSpeedPercent = 30;
    int turnDurationMs = 350;
    int backwardDurationMs = 500;
    int loopDelayMs = 10;
};

struct SetupOptions {
    bool skipSetup = false;
    bool forceSetup = false;
    bool autoConfirm = false;
    fs::path setupMarker;
};

struct TofConfig {
    int alsGainLeft = 7;
    int alsGainRight = 7;
    double defaultSampleInterval = 0.1;
    int maxValidDistanceMm = 255;
    
    // Calibration parameters for tof_calibration
    double calTargetMmLeft = 80.0;
    double calTargetMmRight = 80.0;
    int calSamples = 32;
    double calIntervalSeconds = 0.05;

    // ALS calibration parameters for tof_alts_cal
    double alsCalIntervalSeconds = 1.0;
    int alsCalGainLeft = 7;
    int alsCalGainRight = 7;
    int alsCalSamples = 32;
    double alsCalIntervalSecondsFast = 0.05;
};

struct CommonPaths {
    fs::path dolyRoot;
    fs::path sdkRoot;
    fs::path testDir;
    fs::path dataDir;
    fs::path logsDir;
    fs::path offsetsJson;
    fs::path luxCalibrationJson;
    fs::path referenceOffsetsJson;
    fs::path referenceLuxCalibrationJson;
    fs::path setupMarker;
    fs::path calibrationMarker;
    fs::path luxCalibrationMarker;
    fs::path cruiseConfigIni;
    fs::path mainConfigYaml;
};

class Sensor {
public:
    static int g_max_valid_distance; // 全局生效的范围限制

    Sensor(int i2cBus, int address, int offset = 0);
    ~Sensor();

    Sensor(const Sensor&) = delete;
    Sensor& operator=(const Sensor&) = delete;

    Sensor(Sensor&& other) noexcept;
    Sensor& operator=(Sensor&& other) noexcept;

    int address() const;
    int offset() const;

    void changeAddress(int newAddress);
    void setOffset(int offset);
    int range();
    int rangeRaw();  // 获取原始未过滤的距离
    float readLux(vl6180x_als_gain_t gain);
    void startContinuous(int periodMs);
    void stopContinuous();
    bool continuousEnabled();
    int rangeContinuous(int timeoutMs);
    int rangeFromHistory();
    std::vector<uint8_t> rangeHistory(int count);

private:
    void reset() noexcept;

    int handle_ = -1;
    int address_ = kLeftAddress;
    int offset_ = 0;
};

const CommonPaths& paths();

std::ofstream openLogFile(const std::string& prefix);
void printLine(const std::string& message, std::ofstream* log = nullptr, bool error = false);
std::string nowString();
void sleepMs(int milliseconds);
bool promptYesNo(const std::string& prompt, bool defaultYes = false);

bool checkI2cAddresses(int i2cBus, int addrLeft, int addrRight, std::ofstream* log = nullptr);
void showI2cDetect(int i2cBus, std::ofstream* log = nullptr);
void setTofEnable(bool enabled, std::ofstream* log = nullptr);
bool setupSensors(const SetupOptions& options, std::ofstream* log = nullptr);

OffsetPair loadOffsets(std::ofstream* log = nullptr);
void saveOffsets(const OffsetPair& offsets, std::ofstream* log = nullptr);

LuxCalibration loadLuxCalibration(std::ofstream* log = nullptr);
void saveLuxCalibration(const LuxCalibration& calibration, std::ofstream* log = nullptr);

TofConfig loadTofConfig(std::ofstream* log = nullptr);
CruiseConfig loadCruiseConfig(std::ofstream* log = nullptr);

int clampOffset(double value);
int clampPercent(int value);
bool limitReached(int iteration, int maxIterations);
vl6180x_als_gain_t parseGainValue(int rawGain);
void stopContinuousSafe(Sensor* sensor, const std::string& name, std::ofstream* log = nullptr);

}  // namespace tof_test