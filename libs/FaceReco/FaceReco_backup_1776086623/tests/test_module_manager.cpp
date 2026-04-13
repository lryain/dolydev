#include <gtest/gtest.h>

#include "doly/vision/module_manager.hpp"

// 复用旧体系的 IModule/ModuleState
#include "vision/IModule.h"

using doly::vision::ModuleManager;
using doly::vision::RunMode;

namespace {

class MockModule : public IModule {
public:
    explicit MockModule(std::string name) : name_(std::move(name)) {}

    bool Initialize(const std::map<std::string, std::string>&) override {
        state_ = ModuleState::Ready;
        return true;
    }

    bool Start() override {
        state_ = ModuleState::Running;
        return true;
    }

    bool Process(FrameContext&) override { return true; }

    bool Stop() override {
        state_ = ModuleState::Ready;
        return true;
    }

    bool Unload() override {
        state_ = ModuleState::Uninitialized;
        return true;
    }

    std::string GetModuleName() const override { return name_; }
    std::string GetVersion() const override { return "test"; }
    std::vector<std::string> GetDependencies() const override { return {}; }
    std::vector<std::string> GetProvidedCapabilities() const override { return {}; }
    bool UpdateConfig(const std::map<std::string, std::string>&) override { return true; }
    ModuleState GetState() const override { return state_; }
    std::string GetLastError() const override { return {}; }
    double GetAverageProcessingTime() const override { return 0.0; }
    long GetFramesProcessed() const override { return 0; }
    int GetErrorCount() const override { return 0; }

private:
    std::string name_;
};

}  // namespace

TEST(ModuleManagerTest, CanRegisterAndSwitchMode) {
    ModuleManager mgr;

    EXPECT_TRUE(mgr.registerModule("camera_capture", []() {
        return std::make_shared<MockModule>("camera_capture");
    }));

    EXPECT_TRUE(mgr.registerModule("face_detection", []() {
        return std::make_shared<MockModule>("face_detection");
    }));

    EXPECT_TRUE(mgr.setMode(RunMode::DETECT_ONLY));
    EXPECT_EQ(mgr.mode(), RunMode::DETECT_ONLY);

    auto loaded = mgr.loadedModules();
    EXPECT_GE(loaded.size(), 1u);
}

TEST(ModuleManagerTest, SwitchingToSameModeIsNoop) {
    ModuleManager mgr;

    EXPECT_TRUE(mgr.registerModule("camera_capture", []() {
        return std::make_shared<MockModule>("camera_capture");
    }));

    EXPECT_TRUE(mgr.setMode(RunMode::STREAM_ONLY));
    EXPECT_TRUE(mgr.setMode(RunMode::STREAM_ONLY));
    EXPECT_EQ(mgr.mode(), RunMode::STREAM_ONLY);
}
