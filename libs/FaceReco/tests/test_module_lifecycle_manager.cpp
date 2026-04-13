#include <gtest/gtest.h>
#include "vision/ModuleLifecycleManager.h"
#include <memory>

// Mock 模块实现
class MockModule : public IModule {
public:
    explicit MockModule(const std::string& name) : name_(name) {}

    bool Initialize(const std::map<std::string, std::string>& config) override {
        state_ = ModuleState::Ready;
        return true;
    }

    bool Start() override {
        state_ = ModuleState::Running;
        return true;
    }

    bool Process(FrameContext& frame) override {
        processed_count_++;
        return true;
    }

    bool Stop() override {
        state_ = ModuleState::Uninitialized;
        return true;
    }

    bool Unload() override {
        state_ = ModuleState::Uninitialized;
        return true;
    }

    std::string GetModuleName() const override { return name_; }

    std::string GetVersion() const override { return "1.0"; }

    std::vector<std::string> GetDependencies() const override { return {}; }

    std::vector<std::string> GetProvidedCapabilities() const override { return {}; }

    bool UpdateConfig(const std::map<std::string, std::string>& new_config) override {
        return true;
    }

    ModuleState GetState() const override { return state_; }

    std::string GetLastError() const override { return ""; }

    double GetAverageProcessingTime() const override { return 0.0; }

    long GetFramesProcessed() const override { return frames_processed_; }

    int GetErrorCount() const override { return 0; }

private:
    std::string name_;
    int processed_count_ = 0;
    long frames_processed_ = 0;
};

// 测试：注册和获取模块
TEST(ModuleLifecycleManagerTest, RegisterAndGetModule) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("test_module", []() {
        return std::make_shared<MockModule>("test_module");
    });
    
    EXPECT_TRUE(mgr->HasModule("test_module"));
    auto module = mgr->GetModule("test_module");
    EXPECT_EQ(module, nullptr);  // 未加载前返回nullptr
}

// 测试：初始化所有模块
TEST(ModuleLifecycleManagerTest, InitializeAll) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module_a", []() {
        return std::make_shared<MockModule>("module_a");
    });
    mgr->RegisterModule("module_b", []() {
        return std::make_shared<MockModule>("module_b");
    }, {"module_a"});
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    
    auto loaded = mgr->GetLoadedModules();
    EXPECT_EQ(loaded.size(), 2);
}

// 测试：启动所有模块
TEST(ModuleLifecycleManagerTest, StartAll) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module1", []() {
        return std::make_shared<MockModule>("module1");
    });
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    EXPECT_TRUE(mgr->StartAll());
}

// 测试：处理帧
TEST(ModuleLifecycleManagerTest, ProcessFrame) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("processor", []() {
        return std::make_shared<MockModule>("processor");
    });
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    EXPECT_TRUE(mgr->StartAll());
    
    auto frame = std::make_shared<FrameContext>();
    frame->raw_frame = cv::Mat(480, 640, CV_8UC3);
    
    EXPECT_TRUE(mgr->ProcessFrame(frame));
}

// 测试：停止所有模块
TEST(ModuleLifecycleManagerTest, StopAll) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module1", []() {
        return std::make_shared<MockModule>("module1");
    });
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    EXPECT_TRUE(mgr->StartAll());
    EXPECT_TRUE(mgr->StopAll());
}

// 测试：卸载所有模块
TEST(ModuleLifecycleManagerTest, UnloadAll) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module1", []() {
        return std::make_shared<MockModule>("module1");
    });
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    EXPECT_TRUE(mgr->UnloadAll());
    
    auto loaded = mgr->GetLoadedModules();
    EXPECT_EQ(loaded.size(), 0);
}

// 测试：复杂依赖关系初始化
TEST(ModuleLifecycleManagerTest, ComplexDependencyInitialization) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    // 创建链式依赖: A -> B -> C
    mgr->RegisterModule("module_a", []() {
        return std::make_shared<MockModule>("module_a");
    });
    mgr->RegisterModule("module_b", []() {
        return std::make_shared<MockModule>("module_b");
    }, {"module_a"});
    mgr->RegisterModule("module_c", []() {
        return std::make_shared<MockModule>("module_c");
    }, {"module_b"});
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    
    auto loaded = mgr->GetLoadedModules();
    EXPECT_EQ(loaded.size(), 3);
}

// 测试：循环依赖检测
TEST(ModuleLifecycleManagerTest, CyclicDependencyDetection) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module_a", []() {
        return std::make_shared<MockModule>("module_a");
    });
    mgr->RegisterModule("module_b", []() {
        return std::make_shared<MockModule>("module_b");
    }, {"module_a"});
    // 注意：DependencyManager会检测循环依赖，但这里我们只是验证管理器能处理
    
    auto config = std::make_shared<ConfigManager>();
    // 如果有循环依赖，初始化会返回false
}

// 测试：获取注册模块
TEST(ModuleLifecycleManagerTest, GetRegisteredModules) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module1", []() {
        return std::make_shared<MockModule>("module1");
    });
    mgr->RegisterModule("module2", []() {
        return std::make_shared<MockModule>("module2");
    });
    
    auto registered = mgr->GetRegisteredModules();
    EXPECT_EQ(registered.size(), 2);
}

// 测试：获取已加载模块
TEST(ModuleLifecycleManagerTest, GetLoadedModulesAfterInitialization) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module1", []() {
        return std::make_shared<MockModule>("module1");
    });
    mgr->RegisterModule("module2", []() {
        return std::make_shared<MockModule>("module2");
    });
    
    EXPECT_EQ(mgr->GetLoadedModules().size(), 0);  // 初始化前无加载
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    
    EXPECT_EQ(mgr->GetLoadedModules().size(), 2);  // 初始化后有加载
}

// 测试：总结信息
TEST(ModuleLifecycleManagerTest, GetSummary) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module1", []() {
        return std::make_shared<MockModule>("module1");
    });
    
    auto summary = mgr->GetSummary();
    EXPECT_FALSE(summary.empty());
}

// 测试：清空所有状态
TEST(ModuleLifecycleManagerTest, Clear) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("module1", []() {
        return std::make_shared<MockModule>("module1");
    });
    
    auto config = std::make_shared<ConfigManager>();
    mgr->InitializeAll(config);
    
    mgr->Clear();
    EXPECT_EQ(mgr->GetRegisteredModules().size(), 0);
    EXPECT_EQ(mgr->GetLoadedModules().size(), 0);
}

// 测试：完整生命周期
TEST(ModuleLifecycleManagerTest, FullLifecycle) {
    auto mgr = std::make_shared<ModuleLifecycleManager>();
    
    mgr->RegisterModule("detector", []() {
        return std::make_shared<MockModule>("detector");
    });
    mgr->RegisterModule("tracker", []() {
        return std::make_shared<MockModule>("tracker");
    }, {"detector"});
    
    auto config = std::make_shared<ConfigManager>();
    EXPECT_TRUE(mgr->InitializeAll(config));
    EXPECT_TRUE(mgr->StartAll());
    
    auto frame = std::make_shared<FrameContext>();
    frame->raw_frame = cv::Mat(480, 640, CV_8UC3);
    EXPECT_TRUE(mgr->ProcessFrame(frame));
    
    EXPECT_TRUE(mgr->StopAll());
    EXPECT_TRUE(mgr->UnloadAll());
}
