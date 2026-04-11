#include "test_common.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "Gpio.h"

namespace tof_test {

namespace {

std::string readTextFile(const fs::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void writeTextFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to write file: " + path.string());
    }
    output << content;
}

std::string captureCommand(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("failed to run command: " + command);
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    if (pclose(pipe) == -1) {
        throw std::runtime_error("failed to close command pipe: " + command);
    }
    return output;
}

bool fileExists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error);
}

int extractInt(const std::string& text, const std::string& key, int fallback) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return fallback;
    }
    return std::stoi(match[1].str());
}

double extractDouble(const std::string& text, const std::string& key, double fallback) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return fallback;
    }
    return std::stod(match[1].str());
}

std::string escapeShellPath(const fs::path& path) {
    const std::string raw = path.string();
    std::string escaped;
    escaped.reserve(raw.size() + 2);
    escaped.push_back('\'');
    for (const char ch : raw) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

int extractIniInt(const std::string& text, const std::string& key, int fallback) {
    const std::regex pattern(
        "^\\s*" + key + "\\s*=\\s*([0-9]+)\\s*$",
        std::regex_constants::icase | std::regex_constants::multiline);
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return fallback;
    }
    return std::stoi(match[1].str());
}

double extractIniDouble(const std::string& text, const std::string& key, double fallback) {
    const std::regex pattern(
        "^\\s*" + key + "\\s*=\\s*([0-9]+(?:\\.[0-9]+)?)\\s*$",
        std::regex_constants::icase | std::regex_constants::multiline);
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return fallback;
    }
    return std::stod(match[1].str());
}

std::string joinMessage(const std::string& level, const std::string& message) {
    std::ostringstream output;
    output << nowString() << " [" << level << "] " << message;
    return output.str();
}

}  // namespace

int Sensor::g_max_valid_distance = 255;

Sensor::Sensor(int i2cBus, int address, int offset) : address_(address) {
    handle_ = vl6180_initialise_address(i2cBus, address);
    if (handle_ < 0) {
        throw std::runtime_error("failed to initialize sensor at address 0x" + [&]() {
            std::ostringstream output;
            output << std::hex << std::nouppercase << address;
            return output.str();
        }());
    }
    if (offset != 0) {
        setOffset(offset);
    }
}

Sensor::~Sensor() {
    reset();
}

Sensor::Sensor(Sensor&& other) noexcept {
    handle_ = other.handle_;
    address_ = other.address_;
    offset_ = other.offset_;
    other.handle_ = -1;
}

Sensor& Sensor::operator=(Sensor&& other) noexcept {
    if (this != &other) {
        reset();
        handle_ = other.handle_;
        address_ = other.address_;
        offset_ = other.offset_;
        other.handle_ = -1;
    }
    return *this;
}

int Sensor::address() const {
    return address_;
}

int Sensor::offset() const {
    return offset_;
}

void Sensor::changeAddress(int newAddress) {
    if (vl6180_change_addr(handle_, newAddress) != 0) {
        throw std::runtime_error("failed to change sensor address");
    }
    address_ = newAddress;
}

void Sensor::setOffset(int offset) {
    const int clamped = clampOffset(offset);
    if (vl6180_set_offset(handle_, clamped) != 0) {
        throw std::runtime_error("failed to set sensor offset");
    }
    offset_ = clamped;
}

int Sensor::range() {
    const int val = rangeRaw();
    return (val > g_max_valid_distance) ? g_max_valid_distance : val;
}

int Sensor::rangeRaw() {
    const int value = get_distance(handle_);
    if (value < 0) {
        const int status = vl6180_range_status(handle_);
        std::ostringstream message;
        message << "failed to read distance";
        if (status >= 0) {
            message << " (range_status=" << status << ')';
        }
        throw std::runtime_error(message.str());
    }
    return value;
}

float Sensor::readLux(vl6180x_als_gain_t gain) {
    return get_ambient_light(handle_, gain);
}

void Sensor::startContinuous(int periodMs) {
    if (vl6180_start_range_continuous(handle_, periodMs) != 0) {
        throw std::runtime_error("failed to start continuous mode");
    }
}

void Sensor::stopContinuous() {
    if (vl6180_stop_range_continuous(handle_) != 0) {
        throw std::runtime_error("failed to stop continuous mode");
    }
}

bool Sensor::continuousEnabled() {
    return vl6180_continuous_mode_enabled(handle_) > 0;
}

int Sensor::rangeContinuous(int timeoutMs) {
    const int value = vl6180_get_distance_continuous(handle_, timeoutMs);
    if (value < 0) {
        const int status = vl6180_range_status(handle_);
        std::ostringstream message;
        message << "failed to read continuous distance";
        if (status >= 0) {
            message << " (range_status=" << status << ')';
        }
        throw std::runtime_error(message.str());
    }
    return value;
}

int Sensor::rangeFromHistory() {
    return vl6180_get_range_from_history(handle_);
}

std::vector<uint8_t> Sensor::rangeHistory(int count) {
    std::vector<uint8_t> values(std::max(count, 0));
    if (values.empty()) {
        return values;
    }
    const int written = vl6180_get_ranges_from_history(handle_, values.data(), static_cast<int>(values.size()));
    if (written < 0) {
        throw std::runtime_error("failed to read range history");
    }
    values.resize(static_cast<std::size_t>(written));
    return values;
}

void Sensor::reset() noexcept {
    if (handle_ >= 0) {
        vl6180_close(handle_);
        handle_ = -1;
    }
}

const CommonPaths& paths() {
    static const CommonPaths value = [] {
        CommonPaths output;
        output.dolyRoot = fs::path(DOLYDEV_ROOT);
        output.sdkRoot = fs::path(SDK_ROOT);
        output.testDir = fs::path(VL6180_TEST_SOURCE_DIR);
        output.dataDir = output.testDir / "data";
        output.logsDir = output.dolyRoot / "logs";
        output.offsetsJson = output.dataDir / "tof_offsets.json";
        output.luxCalibrationJson = output.dataDir / "tof_lux_calibration.json";
        output.referenceOffsetsJson = output.dolyRoot / "libs" / "tofpy" / "data" / "tof_offsets.json";
        output.referenceLuxCalibrationJson = output.dolyRoot / "libs" / "tofpy" / "data" / "tof_lux_calibration.json";
        output.setupMarker = "/tmp/tof_demo_setup_done";
        output.calibrationMarker = "/tmp/tof_calibration_setup_done";
        output.luxCalibrationMarker = "/tmp/tof_lux_calibration_done";
        output.cruiseConfigIni = output.dolyRoot / "config" / "tof_cruise.ini";
        output.mainConfigYaml = output.dolyRoot / "config" / "tof.yaml";
        return output;
    }();
    return value;
}

std::ofstream openLogFile(const std::string& prefix) {
    const auto& common = paths();
    fs::create_directories(common.logsDir);
    const auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::ostringstream filename;
    filename << prefix << '_' << timestamp << ".log";
    std::ofstream output(common.logsDir / filename.str(), std::ios::app);
    return output;
}

void printLine(const std::string& message, std::ofstream* log, bool error) {
    std::ostream& stream = error ? std::cerr : std::cout;
    stream << message << std::endl;
    if (log != nullptr && *log) {
        *log << joinMessage(error ? "ERR" : "INFO", message) << '\n';
        log->flush();
    }
}

std::string nowString() {
    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&timestamp, &localTime);
    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void sleepMs(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

bool promptYesNo(const std::string& prompt, bool defaultYes) {
    std::cout << prompt;
    std::string answer;
    std::getline(std::cin, answer);
    if (answer.empty()) {
        return defaultYes;
    }
    const std::string lower = [&]() {
        std::string value = answer;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }();
    return lower == "y" || lower == "yes";
}

bool checkI2cAddresses(int i2cBus, int addrLeft, int addrRight, std::ofstream* log) {
    try {
        std::ostringstream command;
        command << "i2cdetect -y " << i2cBus << " 2>/dev/null";
        const std::string output = captureCommand(command.str());

        const auto containsHex = [&](int address) {
            std::ostringstream token;
            token << std::hex << std::nouppercase << std::setw(2) << std::setfill('0') << address;
            return output.find(token.str()) != std::string::npos;
        };

        const bool leftPresent = containsHex(addrLeft);
        const bool rightPresent = containsHex(addrRight);
        std::ostringstream message;
        message << "i2cdetect address check: left=" << leftPresent << " right=" << rightPresent;
        printLine(message.str(), log);
        return leftPresent && rightPresent;
    } catch (const std::exception& error) {
        printLine(std::string("failed to run i2cdetect: ") + error.what(), log, true);
        return false;
    }
}

void showI2cDetect(int i2cBus, std::ofstream* log) {
    try {
        std::ostringstream command;
        command << "i2cdetect -y " << i2cBus;
        const std::string output = captureCommand(command.str());
        if (log != nullptr && *log) {
            *log << joinMessage("INFO", "i2cdetect output") << '\n' << output << std::flush;
        }
        std::cout << output;
    } catch (const std::exception& error) {
        printLine(std::string("failed to display i2cdetect output: ") + error.what(), log, true);
    }
}

void setTofEnable(bool enabled, std::ofstream* log) {
    const auto initialState = enabled ? HIGH : LOW;
    if (GPIO::init(TOF_ENL, GPIO_OUTPUT, initialState) != 0) {
        throw std::runtime_error("failed to initialize TOF_ENL gpio");
    }
    if (GPIO::writePin(TOF_ENL, enabled ? HIGH : LOW) != 0) {
        throw std::runtime_error("failed to write TOF_ENL gpio");
    }
    printLine(std::string("TOF_ENL -> ") + (enabled ? "on" : "off"), log);
}

bool setupSensors(const SetupOptions& options, std::ofstream* log) {
    const auto& common = paths();
    const fs::path marker = options.setupMarker.empty() ? common.setupMarker : options.setupMarker;

    if (options.skipSetup) {
        printLine("skip_setup enabled, skipping address setup", log);
        return false;
    }

    if (options.forceSetup && fileExists(marker)) {
        fs::remove(marker);
        printLine("removed existing setup marker: " + marker.string(), log);
    }

    if (checkI2cAddresses(kI2cBus, kLeftAddress, kRightAddress, log)) {
        std::ostringstream message;
        message << "setup_sensors: detected 0x" << std::hex << kLeftAddress << " and 0x" << kRightAddress
                << " already present; skipping setup";
        printLine(message.str(), log);
        return false;
    }

    setTofEnable(false, log);
    sleepMs(20);
    printLine("先关闭左侧 TOF_ENL，此时应只看到一个默认地址传感器。", log);
    showI2cDetect(kI2cBus, log);

    Sensor rightOnly(kI2cBus, kLeftAddress);
    if (!options.autoConfirm && !promptYesNo("即将把右侧传感器地址从 0x29 改成 0x30，确认？(y/n): ", false)) {
        throw std::runtime_error("address change cancelled by user");
    }

    rightOnly.changeAddress(kRightAddress);
    sleepMs(20);
    printLine("右侧地址已切换到 0x30。", log);
    showI2cDetect(kI2cBus, log);

    setTofEnable(true, log);
    sleepMs(50);
    printLine("重新打开左侧后，i2cdetect 应同时看到 0x29 和 0x30。", log);
    showI2cDetect(kI2cBus, log);

    if (!checkI2cAddresses(kI2cBus, kLeftAddress, kRightAddress, log)) {
        throw std::runtime_error("setup complete but i2c addresses are still incomplete");
    }

    writeTextFile(marker, std::to_string(std::time(nullptr)) + "\n");
    printLine("setup marker written: " + marker.string(), log);
    return true;
}

OffsetPair loadOffsets(std::ofstream* log) {
    const auto& common = paths();
    const fs::path source = fileExists(common.offsetsJson) ? common.offsetsJson : common.referenceOffsetsJson;
    if (!fileExists(source)) {
        printLine("offset file missing, using zeros", log);
        return {};
    }

    try {
        const std::string text = readTextFile(source);
        OffsetPair offsets;
        offsets.left = extractInt(text, "left", 0);
        offsets.right = extractInt(text, "right", 0);
        return offsets;
    } catch (const std::exception& error) {
        printLine(std::string("failed to read offsets: ") + error.what(), log, true);
        return {};
    }
}

void saveOffsets(const OffsetPair& offsets, std::ofstream* log) {
    std::ostringstream content;
    content << "{\"left\": " << offsets.left << ", \"right\": " << offsets.right << "}";
    writeTextFile(paths().offsetsJson, content.str());
    printLine("saved offsets to " + paths().offsetsJson.string(), log);
}

LuxCalibration loadLuxCalibration(std::ofstream* log) {
    const auto& common = paths();
    const fs::path source = fileExists(common.luxCalibrationJson) ? common.luxCalibrationJson : common.referenceLuxCalibrationJson;
    if (!fileExists(source)) {
        printLine("lux calibration file missing, using unity scale", log);
        return {};
    }

    try {
        const std::string text = readTextFile(source);
        LuxCalibration calibration;
        calibration.leftScale = extractDouble(text, "left_scale", 1.0);
        calibration.rightScale = extractDouble(text, "right_scale", 1.0);
        calibration.leftRaw = extractDouble(text, "left_raw", 0.0);
        calibration.rightRaw = extractDouble(text, "right_raw", 0.0);
        if (calibration.leftScale <= 0.0 || calibration.rightScale <= 0.0) {
            calibration.leftScale = 1.0;
            calibration.rightScale = 1.0;
        }
        return calibration;
    } catch (const std::exception& error) {
        printLine(std::string("failed to read lux calibration: ") + error.what(), log, true);
        return {};
    }
}

void saveLuxCalibration(const LuxCalibration& calibration, std::ofstream* log) {
    std::ostringstream content;
    content << std::fixed << std::setprecision(6);
    content << "{"
            << "\"left_scale\": " << calibration.leftScale << ", "
            << "\"right_scale\": " << calibration.rightScale << ", "
            << "\"left_raw\": " << calibration.leftRaw << ", "
            << "\"right_raw\": " << calibration.rightRaw << ", "
            << "\"updated_at\": " << std::time(nullptr)
            << "}";
    writeTextFile(paths().luxCalibrationJson, content.str());
    printLine("saved lux calibration to " + paths().luxCalibrationJson.string(), log);
}

TofConfig loadTofConfig(std::ofstream* log) {
    const auto& common = paths();
    TofConfig config;
    if (fileExists(common.mainConfigYaml)) {
        try {
            YAML::Node root = YAML::LoadFile(common.mainConfigYaml.string());
            if (root["tof"]) {
                YAML::Node tof = root["tof"];
                config.alsGainLeft = tof["als_gain_left"].as<int>(config.alsGainLeft);
                config.alsGainRight = tof["als_gain_right"].as<int>(config.alsGainRight);
                config.defaultSampleInterval = tof["default_sample_interval"].as<double>(config.defaultSampleInterval);
                config.maxValidDistanceMm = tof["max_valid_distance_mm"].as<int>(config.maxValidDistanceMm);
                Sensor::g_max_valid_distance = config.maxValidDistanceMm;

                if (tof["calibration"]) {
                    YAML::Node cal = tof["calibration"];
                    config.calTargetMmLeft = cal["target_mm_left"].as<double>(config.calTargetMmLeft);
                    config.calTargetMmRight = cal["target_mm_right"].as<double>(config.calTargetMmRight);
                    config.calSamples = cal["samples"].as<int>(config.calSamples);
                    config.calIntervalSeconds = cal["interval_seconds"].as<double>(config.calIntervalSeconds);
                }

                if (tof["alts_calibration"]) {
                    YAML::Node als_cal = tof["alts_calibration"];
                    config.alsCalIntervalSeconds = als_cal["interval_seconds"].as<double>(config.alsCalIntervalSeconds);
                    config.alsCalGainLeft = als_cal["gain_left"].as<int>(config.alsCalGainLeft);
                    config.alsCalGainRight = als_cal["gain_right"].as<int>(config.alsCalGainRight);
                    config.alsCalSamples = als_cal["samples"].as<int>(config.alsCalSamples);
                    config.alsCalIntervalSecondsFast = als_cal["sample_interval_seconds"].as<double>(config.alsCalIntervalSecondsFast);
                }

                printLine("Loaded general TOF config from " + common.mainConfigYaml.filename().string(), log);
            }
        } catch (const std::exception& e) {
            printLine("Error parsing TOF config in " + common.mainConfigYaml.string() + ": " + e.what(), log, true);
        }
    }
    return config;
}

CruiseConfig loadCruiseConfig(std::ofstream* log) {
    const auto& common = paths();
    CruiseConfig config;

    if (fileExists(common.mainConfigYaml)) {
        try {
            YAML::Node root = YAML::LoadFile(common.mainConfigYaml.string());
            if (root["tof"] && root["tof"]["cruise"]) {
                YAML::Node cruise = root["tof"]["cruise"];
                config.obstacleMm = cruise["obstacle_mm"].as<int>(config.obstacleMm);
                config.diffThresholdMm = cruise["diff_threshold_mm"].as<int>(config.diffThresholdMm);
                config.forwardSpeedPercent = cruise["forward_speed_percent"].as<int>(config.forwardSpeedPercent);
                config.turnSpeedPercent = cruise["turn_speed_percent"].as<int>(config.turnSpeedPercent);
                config.backwardSpeedPercent = cruise["backward_speed_percent"].as<int>(config.backwardSpeedPercent);
                config.turnDurationMs = cruise["turn_duration_ms"].as<int>(config.turnDurationMs);
                config.backwardDurationMs = cruise["backward_duration_ms"].as<int>(config.backwardDurationMs);
                config.loopDelayMs = cruise["loop_delay_ms"].as<int>(config.loopDelayMs);
                printLine("Loaded cruise config from " + common.mainConfigYaml.filename().string(), log);
                return config;
            }
        } catch (const std::exception& e) {
            printLine("Error parsing " + common.mainConfigYaml.string() + ": " + e.what(), log, true);
        }
    }

    const auto& path = common.cruiseConfigIni;
    if (!fileExists(path)) {
        printLine("cruise config files missing, using built-in defaults", log);
        return config;
    }

    try {
        const std::string text = readTextFile(path);
        config.obstacleMm = extractIniInt(text, "obstacle_mm", config.obstacleMm);
        config.forwardSpeedPercent = clampPercent(static_cast<int>(std::lround(extractIniDouble(text, "forward_speed", 0.4) * 100.0)));
        config.turnSpeedPercent = clampPercent(static_cast<int>(std::lround(extractIniDouble(text, "turn_speed", 0.4) * 100.0)));
        config.turnDurationMs = std::max(1, static_cast<int>(std::lround(extractIniDouble(text, "turn_time_s", 0.35) * 1000.0)));
        config.loopDelayMs = std::max(1, static_cast<int>(std::lround(extractIniDouble(text, "loop_delay_s", 0.05) * 1000.0)));
        printLine("Loaded cruise config from " + path.filename().string(), log);
        return config;
    } catch (const std::exception& error) {
        printLine(std::string("failed to parse cruise config: ") + error.what(), log, true);
        return config;
    }
}

int clampOffset(double value) {
    return std::max(-128, std::min(127, static_cast<int>(std::lround(value))));
}

int clampPercent(int value) {
    return std::max(0, std::min(100, value));
}

bool limitReached(int iteration, int maxIterations) {
    return maxIterations > 0 && iteration >= maxIterations;
}

vl6180x_als_gain_t parseGainValue(int rawGain) {
    if (rawGain >= 0 && rawGain <= 7) {
        return static_cast<vl6180x_als_gain_t>(rawGain);
    }

    switch (rawGain) {
        case 20:
            return GAIN_20;
        case 10:
            return GAIN_10;
        case 5:
            return GAIN_5;
        case 25:
            return GAIN_2_5;
        case 167:
            return GAIN_1_67;
        case 125:
            return GAIN_1_25;
        case 100:
            return GAIN_1;
        case 40:
            return GAIN_40;
        default:
            throw std::invalid_argument("unsupported gain value");
    }
}

void stopContinuousSafe(Sensor* sensor, const std::string& name, std::ofstream* log) {
    if (sensor == nullptr) {
        return;
    }
    try {
        sensor->stopContinuous();
        sleepMs(300);
        printLine("stopped continuous mode for " + name, log);
    } catch (const std::exception& error) {
        printLine("failed to stop continuous mode for " + name + ": " + error.what(), log, true);
    }
}

}  // namespace tof_test