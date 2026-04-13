#include "vision/noop_module.hpp"

NoopModule::NoopModule(std::string name)
    : name_(std::move(name)) {}

bool NoopModule::Initialize(const std::map<std::string, std::string>&) {
    state_ = ModuleState::Ready;
    start_time_ = std::chrono::steady_clock::now();
    return true;
}

bool NoopModule::Start() {
    state_ = ModuleState::Running;
    return true;
}

bool NoopModule::Process(FrameContext&) {
    ++frames_processed_;
    return true;
}

bool NoopModule::Stop() {
    state_ = ModuleState::Ready;
    return true;
}

bool NoopModule::Unload() {
    state_ = ModuleState::Uninitialized;
    return true;
}

std::string NoopModule::GetModuleName() const { return name_; }

std::string NoopModule::GetVersion() const { return "noop-0.1"; }

std::vector<std::string> NoopModule::GetDependencies() const { return {}; }

std::vector<std::string> NoopModule::GetProvidedCapabilities() const { return {}; }

bool NoopModule::UpdateConfig(const std::map<std::string, std::string>&) {
    return true;
}

ModuleState NoopModule::GetState() const { return state_; }

std::string NoopModule::GetLastError() const { return last_error_; }

double NoopModule::GetAverageProcessingTime() const { return avg_processing_time_ms_; }

long NoopModule::GetFramesProcessed() const { return frames_processed_; }

int NoopModule::GetErrorCount() const { return error_count_; }
