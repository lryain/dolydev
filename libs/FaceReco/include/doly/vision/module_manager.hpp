#pragma once

#include "doly/vision/module_types.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// 依赖旧的 vision/* 体系（已有 gtest 与生命周期管理）
#include "vision/ModuleLifecycleManager.h"

namespace doly::vision {

/**
 * @brief Vision Service 的模块管理器（M2 核心）
 *
 * 目标：
 *  - 以 RunMode 为入口，完成模块集合的装载/启动/停止/卸载。
 *  - 对外提供状态快照（用于 status.vision.state）。
 *
 * 说明：当前实现先做“最小可用骨架”，实际模块工厂注册（摄像头/推流/检测/识别等）
 *       会在后续 M2 任务里补全。
 */
class ModuleManager {
public:
    struct Options {
        RunMode initial_mode{RunMode::FULL};
    };

    ModuleManager();
    explicit ModuleManager(Options options);
    ~ModuleManager();

    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /**
     * @brief 注册“逻辑模块”（当前用字符串 key 来复用既有 ModuleLifecycleManager）。
     *
     * 约定：module_key 建议使用与 docs/协议一致的语义名，例如：
     *  - "camera_capture"
     *  - "video_stream"
     *  - "face_detection"
     *  - "face_tracking"
     *  - "face_recognition"
     */
    bool registerModule(const std::string& module_key,
                        std::function<std::shared_ptr<IModule>()> factory,
                        const std::vector<std::string>& dependencies = {});

    /**
     * @brief 切换 run mode（会停止并卸载当前模式模块，再装载新模式模块）。
     */
    bool setMode(RunMode mode);
    [[nodiscard]] RunMode mode() const;

    /**
     * @brief 返回当前装载的模块 key 列表
     */
    std::vector<std::string> loadedModules() const;

    /**
     * @brief 生成状态快照（模块粒度先返回 key->ModuleState 的粗略映射）
     */
    VisionServiceState snapshot() const;

private:
    bool applyModeLocked(RunMode mode);
    std::vector<std::string> resolveModulesForMode(RunMode mode) const;
    static std::string moduleKeyForType(ModuleType type);
    static std::optional<ModuleType> moduleTypeForKey(const std::string& key);

private:
    mutable std::mutex mutex_;
    Options options_;

    RunMode mode_{RunMode::IDLE};
    std::shared_ptr<ModuleLifecycleManager> lifecycle_;
};

}  // namespace doly::vision
