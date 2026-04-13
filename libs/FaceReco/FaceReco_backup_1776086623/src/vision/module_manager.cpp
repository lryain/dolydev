#include "doly/vision/module_manager.hpp"

#include <unordered_set>
#include <iostream>

namespace doly::vision {

ModuleManager::ModuleManager()
    : ModuleManager(Options{}) {}

ModuleManager::ModuleManager(Options options)
    : options_(options)
    , lifecycle_(CreateModuleLifecycleManager()) {
    mode_ = RunMode::IDLE;
}

ModuleManager::~ModuleManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lifecycle_) {
        lifecycle_->StopAll();
        lifecycle_->UnloadAll();
    }
}

bool ModuleManager::registerModule(const std::string& module_key,
                                   std::function<std::shared_ptr<IModule>()> factory,
                                   const std::vector<std::string>& dependencies) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lifecycle_) {
        return false;
    }
    return lifecycle_->RegisterModule(module_key, std::move(factory), dependencies);
}

bool ModuleManager::setMode(RunMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    return applyModeLocked(mode);
}

RunMode ModuleManager::mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_;
}

std::vector<std::string> ModuleManager::loadedModules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lifecycle_) {
        return {};
    }
    return lifecycle_->GetLoadedModules();
}

VisionServiceState ModuleManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    VisionServiceState state{};
    state.enabled = true;
    state.mode = mode_;
    state.streaming = (mode_ == RunMode::STREAM_ONLY || mode_ == RunMode::FULL);
    state.fps = 0.0;
    state.active_tracks = 0;
    state.recognized_count = 0;
    state.uptime_seconds = 0;

    // 目前 ModuleLifecycleManager 的状态机属于旧体系，这里先做一个粗略映射。
    for (const auto& key : lifecycle_->GetRegisteredModules()) {
        auto module = lifecycle_->GetModule(key);
        auto type_opt = moduleTypeForKey(key);
        if (!type_opt.has_value()) {
            continue;
        }
        auto type = type_opt.value();

        if (!module) {
            state.module_states.emplace(type, ModuleState::UNLOADED);
            continue;
        }

        auto ms = module->GetState();
        ModuleState mapped = ModuleState::UNLOADED;
        if (ms == ::ModuleState::Running) {
            mapped = ModuleState::RUNNING;
        } else if (ms == ::ModuleState::Ready) {
            mapped = ModuleState::PAUSED;
        } else if (ms == ::ModuleState::Error) {
            mapped = ModuleState::ERROR;
        }
        state.module_states.emplace(type, mapped);
    }

    return state;
}

bool ModuleManager::applyModeLocked(RunMode mode) {
    if (!lifecycle_) {
        return false;
    }

    if (mode_ == mode) {
        return true;
    }

    std::cerr << "[ModuleManager::applyModeLocked] 🔄 开始模式切换: " 
              << static_cast<int>(mode_) << " → " << static_cast<int>(mode) << std::endl;

    // 当前策略：全停全卸载，再按新模式"选择性"初始化并启动。
    std::cerr << "[ModuleManager] 📍 1. 调用 StopAll()..." << std::endl;
    if (!lifecycle_->StopAll()) {
        std::cerr << "[ModuleManager] ❌ StopAll() 返回 false！" << std::endl;
        // ❌ 不应该在这里直接返回 false，因为 IDLE 模式下 init_order_ 为空，StopAll 会返回成功
        // return false;
    }
    std::cerr << "[ModuleManager] ✅ StopAll() 完成" << std::endl;

    std::cerr << "[ModuleManager] 📍 2. 调用 UnloadAll()..." << std::endl;
    if (!lifecycle_->UnloadAll()) {
        std::cerr << "[ModuleManager] ⚠️ UnloadAll() 返回 false（这通常是安全的）" << std::endl;
        // 不返回，继续执行，因为模块可能已经卸载了
    }
    std::cerr << "[ModuleManager] ✅ UnloadAll() 完成" << std::endl;

    auto desired_modules = resolveModulesForMode(mode);
    std::cerr << "[ModuleManager] 📍 3. 目标模块数量: " << desired_modules.size() << std::endl;
    
    if (!desired_modules.empty()) {
        auto cfg = CreateConfigManager();
        
        std::cerr << "[ModuleManager] 📍 4. 调用 InitializeSelected()..." << std::endl;
        if (!lifecycle_->InitializeSelected(cfg, desired_modules)) {
            std::cerr << "[ModuleManager] ❌ InitializeSelected() 返回 false！" << std::endl;
            return false;
        }
        std::cerr << "[ModuleManager] ✅ InitializeSelected() 完成" << std::endl;

        std::cerr << "[ModuleManager] 📍 5. 调用 StartSelected()..." << std::endl;
        if (!lifecycle_->StartSelected(desired_modules)) {
            std::cerr << "[ModuleManager] ❌ StartSelected() 返回 false！" << std::endl;
            return false;
        }
        std::cerr << "[ModuleManager] ✅ StartSelected() 完成" << std::endl;
    } else {
        std::cerr << "[ModuleManager] ℹ️ 目标模式为 IDLE，无需初始化模块" << std::endl;
    }

    mode_ = mode;
    std::cerr << "[ModuleManager] ✅ 模式切换完成: " << static_cast<int>(mode) << std::endl;
    return true;
}

std::vector<std::string> ModuleManager::resolveModulesForMode(RunMode mode) const {
    std::vector<std::string> keys;
    for (auto type : getModulesForMode(mode)) {
        keys.push_back(moduleKeyForType(type));
    }
    return keys;
}

std::string ModuleManager::moduleKeyForType(ModuleType type) {
    switch (type) {
        case ModuleType::CAMERA_CAPTURE:     return "camera_capture";
        case ModuleType::VIDEO_STREAM:       return "video_stream";
        case ModuleType::FACE_DETECTION:     return "face_detection";
        case ModuleType::FACE_TRACKING:      return "face_tracking";
        case ModuleType::FACE_RECOGNITION:   return "face_recognition";
        case ModuleType::LIVENESS_DETECTION: return "liveness_detection";
        case ModuleType::PHOTO_CAPTURE:      return "photo_capture";
        case ModuleType::VIDEO_RECORD:       return "video_record";
        default:                             return "unknown";
    }
}

std::optional<ModuleType> ModuleManager::moduleTypeForKey(const std::string& key) {
    if (key == "camera_capture") return ModuleType::CAMERA_CAPTURE;
    if (key == "video_stream") return ModuleType::VIDEO_STREAM;
    if (key == "face_detection") return ModuleType::FACE_DETECTION;
    if (key == "face_tracking") return ModuleType::FACE_TRACKING;
    if (key == "face_recognition") return ModuleType::FACE_RECOGNITION;
    if (key == "liveness_detection") return ModuleType::LIVENESS_DETECTION;
    if (key == "photo_capture") return ModuleType::PHOTO_CAPTURE;
    if (key == "video_record") return ModuleType::VIDEO_RECORD;
    return std::nullopt;
}

}  // namespace doly::vision
