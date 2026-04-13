#pragma once

#include "vision/IModule.h"
#include <string>
#include <vector>

class NoopModule : public IModule {
public:
    explicit NoopModule(std::string name);

    bool Initialize(const std::map<std::string, std::string>& config) override;
    bool Start() override;
    bool Process(FrameContext& frame_ctx) override;
    bool Stop() override;
    bool Unload() override;

    std::string GetModuleName() const override;
    std::string GetVersion() const override;
    std::vector<std::string> GetDependencies() const override;
    std::vector<std::string> GetProvidedCapabilities() const override;

    bool UpdateConfig(const std::map<std::string, std::string>& new_config) override;
    ModuleState GetState() const override;
    std::string GetLastError() const override;

    double GetAverageProcessingTime() const override;
    long GetFramesProcessed() const override;
    int GetErrorCount() const override;

private:
    std::string name_;
};
