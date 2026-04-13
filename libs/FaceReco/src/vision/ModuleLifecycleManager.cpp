#include "vision/ModuleLifecycleManager.h"
#include <algorithm>
#include <unordered_set>
#include <sstream>
#include <iostream>

ModuleLifecycleManager::ModuleLifecycleManager()
    : dep_mgr_(CreateDependencyManager()) {}

ModuleLifecycleManager::~ModuleLifecycleManager() {
    UnloadAll();
}

bool ModuleLifecycleManager::RegisterModule(
    const std::string& module_name,
    std::function<std::shared_ptr<IModule>()> factory,
    const std::vector<std::string>& dependencies) {
    
    if (module_name.empty() || !factory) return false;
    
    factories_[module_name] = factory;
    dep_mgr_->RegisterModule(module_name, dependencies);
    return true;
}

bool ModuleLifecycleManager::HasModule(const std::string& module_name) const {
    return factories_.find(module_name) != factories_.end();
}

std::shared_ptr<IModule> ModuleLifecycleManager::GetModule(
    const std::string& module_name) const {
    auto it = modules_.find(module_name);
    if (it != modules_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ModuleLifecycleManager::InitializeAll(ConfigManagerPtr config) {
    // 检查循环依赖
    if (dep_mgr_->HasCyclicDependency()) {
        return false;
    }

    // 获取初始化顺序
    init_order_ = dep_mgr_->GetInitializationOrder();
    if (init_order_.empty() && !factories_.empty()) {
        return false;
    }

    // 按顺序初始化
    for (const auto& module_name : init_order_) {
        if (!LoadModule(module_name)) {
            return false;
        }
        
        auto module = modules_[module_name];
        std::map<std::string, std::string> conf_map;
        if (!module->Initialize(conf_map)) {
            return false;
        }
    }

    return true;
}

std::vector<std::string> ModuleLifecycleManager::ResolveDependencyClosure(
    const std::vector<std::string>& selected_modules) const {
    std::unordered_set<std::string> closure;
    std::vector<std::string> stack = selected_modules;

    while (!stack.empty()) {
        auto cur = stack.back();
        stack.pop_back();
        if (cur.empty()) {
            continue;
        }
        if (closure.insert(cur).second) {
            // 追加依赖
            auto deps = dep_mgr_->GetDependencies(cur);
            for (const auto& d : deps) {
                if (!d.empty()) {
                    stack.push_back(d);
                }
            }
        }
    }

    return std::vector<std::string>(closure.begin(), closure.end());
}

bool ModuleLifecycleManager::InitializeSelected(ConfigManagerPtr config,
                                               const std::vector<std::string>& selected_modules) {
    if (dep_mgr_->HasCyclicDependency()) {
        return false;
    }

    const auto closure = ResolveDependencyClosure(selected_modules);
    if (closure.empty()) {
        init_order_.clear();
        return true;
    }

    // 全局初始化顺序（拓扑序）
    const auto global_order = dep_mgr_->GetInitializationOrder();
    if (global_order.empty() && !factories_.empty()) {
        return false;
    }

    std::unordered_set<std::string> closure_set(closure.begin(), closure.end());
    init_order_.clear();
    for (const auto& name : global_order) {
        if (closure_set.count(name)) {
            init_order_.push_back(name);
        }
    }

    // 初始化闭包内模块
    for (const auto& module_name : init_order_) {
        if (!LoadModule(module_name)) {
            return false;
        }
        auto module = modules_[module_name];
        std::map<std::string, std::string> conf_map;
        if (!module->Initialize(conf_map)) {
            return false;
        }
    }

    return true;
}

bool ModuleLifecycleManager::LoadModule(const std::string& module_name) {
    if (modules_.find(module_name) != modules_.end()) {
        return true;  // 已加载
    }

    auto it = factories_.find(module_name);
    if (it == factories_.end()) {
        return false;
    }

    auto module = it->second();
    if (!module) {
        return false;
    }

    modules_[module_name] = module;
    return true;
}

bool ModuleLifecycleManager::StartAll() {
    for (const auto& module_name : init_order_) {
        auto module = modules_[module_name];
        if (!module->Start()) {
            return false;
        }
    }
    return true;
}

bool ModuleLifecycleManager::StartSelected(const std::vector<std::string>& selected_modules) {
    std::unordered_set<std::string> selected(selected_modules.begin(), selected_modules.end());
    for (const auto& module_name : init_order_) {
        if (!selected.empty() && !selected.count(module_name)) {
            continue;
        }
        auto it = modules_.find(module_name);
        if (it == modules_.end()) {
            continue;
        }
        if (!it->second->Start()) {
            return false;
        }
    }
    return true;
}

bool ModuleLifecycleManager::ProcessFrame(FrameContextPtr frame) {
    if (!frame || !frame->IsValid()) {
        return false;
    }

    for (const auto& module_name : init_order_) {
        if (!ExecuteModuleProcessing(module_name, frame)) {
            return false;
        }
    }

    return true;
}

bool ModuleLifecycleManager::ExecuteModuleProcessing(
    const std::string& module_name, FrameContextPtr frame) {
    auto it = modules_.find(module_name);
    if (it == modules_.end()) {
        return false;
    }

    auto module = it->second;
    if (module->GetState() != ModuleState::Running) {
        return false;
    }

    return module->Process(*frame);
}

bool ModuleLifecycleManager::StopAll() {
    std::cerr << "[ModuleLifecycleManager::StopAll] 🛑 开始停止所有模块 (共 " << init_order_.size() << " 个)" << std::endl;
    
    // 如果没有模块加载，直接返回成功
    if (init_order_.empty()) {
        std::cerr << "[ModuleLifecycleManager::StopAll] ℹ️ 没有加载的模块，直接返回成功" << std::endl;
        return true;
    }
    
    for (size_t i = 0; i < init_order_.size(); ++i) {
        const auto& module_name = init_order_[i];
        std::cerr << "[ModuleLifecycleManager::StopAll] 📍 停止模块 [" << (i+1) << "/" << init_order_.size() 
                  << "]: " << module_name << std::endl;
        
        auto module = modules_[module_name];
        if (!module) {
            std::cerr << "[ModuleLifecycleManager::StopAll] ⚠️ 模块为空: " << module_name << " (这可能表示模块被卸载了)" << std::endl;
            continue;  // 改为 continue 而不是 return false，因为模块已经停止了
        }
        
        std::cerr << "[ModuleLifecycleManager::StopAll] 🔄 调用 " << module_name << "->Stop()..." << std::endl;
        if (!module->Stop()) {
            std::cerr << "[ModuleLifecycleManager::StopAll] ❌ " << module_name << "->Stop() 返回 false！" << std::endl;
            return false;
        }
        std::cerr << "[ModuleLifecycleManager::StopAll] ✅ " << module_name << " 已停止" << std::endl;
    }
    
    std::cerr << "[ModuleLifecycleManager::StopAll] ✅ 所有模块已停止" << std::endl;
    return true;
}

bool ModuleLifecycleManager::StopSelected(const std::vector<std::string>& selected_modules) {
    std::unordered_set<std::string> selected(selected_modules.begin(), selected_modules.end());
    // 逆序停
    for (auto it = init_order_.rbegin(); it != init_order_.rend(); ++it) {
        const auto& module_name = *it;
        if (!selected.empty() && !selected.count(module_name)) {
            continue;
        }
        auto mit = modules_.find(module_name);
        if (mit == modules_.end()) {
            continue;
        }
        if (!mit->second->Stop()) {
            return false;
        }
    }
    return true;
}

bool ModuleLifecycleManager::UnloadAll() {
    std::cerr << "[ModuleLifecycleManager::UnloadAll] 🗑️ 开始卸载所有模块 (已加载: " << modules_.size() << ")" << std::endl;
    
    auto unload_order = dep_mgr_->GetUnloadOrder();
    std::reverse(unload_order.begin(), unload_order.end());
    
    std::cerr << "[ModuleLifecycleManager::UnloadAll] 🔄 卸载顺序: ";
    for (const auto& m : unload_order) std::cerr << m << " ";
    std::cerr << std::endl;
    
    for (size_t i = 0; i < unload_order.size(); ++i) {
        const auto& module_name = unload_order[i];
        auto it = modules_.find(module_name);
        
        if (it != modules_.end()) {
            std::cerr << "[ModuleLifecycleManager::UnloadAll] 📍 卸载模块 [" << (i+1) << "/" << unload_order.size() 
                      << "]: " << module_name << std::endl;
            
            std::cerr << "[ModuleLifecycleManager::UnloadAll] 🔄 调用 " << module_name << "->Unload()..." << std::endl;
            it->second->Unload();
            modules_.erase(it);
            std::cerr << "[ModuleLifecycleManager::UnloadAll] ✅ " << module_name << " 已卸载" << std::endl;
        } else {
            std::cerr << "[ModuleLifecycleManager::UnloadAll] ℹ️ 模块未加载: " << module_name << std::endl;
        }
    }
    
    std::cerr << "[ModuleLifecycleManager::UnloadAll] ✅ 所有模块已卸载 (剩余: " << modules_.size() << ")" << std::endl;
    
    // 🔥 关键修复：清空 init_order_，保持与 modules_ 同步
    std::cerr << "[ModuleLifecycleManager::UnloadAll] 🧹 清空 init_order_，当前大小: " << init_order_.size() << std::endl;
    init_order_.clear();
    std::cerr << "[ModuleLifecycleManager::UnloadAll] ✅ init_order_ 已清空 (新大小: " << init_order_.size() << ")" << std::endl;
    
    return true;
}

bool ModuleLifecycleManager::UnloadSelected(const std::vector<std::string>& selected_modules) {
    auto unload_order = dep_mgr_->GetUnloadOrder();
    std::reverse(unload_order.begin(), unload_order.end());

    const auto closure = ResolveDependencyClosure(selected_modules);
    std::unordered_set<std::string> closure_set(closure.begin(), closure.end());

    for (const auto& module_name : unload_order) {
        if (!closure_set.count(module_name)) {
            continue;
        }
        auto it = modules_.find(module_name);
        if (it != modules_.end()) {
            it->second->Unload();
            modules_.erase(it);
        }
    }

    // 更新 init_order_：移除已经卸载的模块
    init_order_.erase(
        std::remove_if(init_order_.begin(), init_order_.end(), [&](const std::string& n) {
            return closure_set.count(n) > 0;
        }),
        init_order_.end());

    return true;
}

std::vector<std::string> ModuleLifecycleManager::GetLoadedModules() const {
    std::vector<std::string> loaded;
    for (const auto& [name, _] : modules_) {
        loaded.push_back(name);
    }
    return loaded;
}

std::vector<std::string> ModuleLifecycleManager::GetRegisteredModules() const {
    std::vector<std::string> registered;
    for (const auto& [name, _] : factories_) {
        registered.push_back(name);
    }
    return registered;
}

std::string ModuleLifecycleManager::GetSummary() const {
    std::ostringstream oss;
    oss << "ModuleLifecycleManager Summary:\n";
    oss << "  Registered Modules: " << factories_.size() << "\n";
    oss << "  Loaded Modules: " << modules_.size() << "\n";
    oss << "  Initialization Order: ";
    for (const auto& name : init_order_) {
        oss << name << " -> ";
    }
    oss << "END\n";
    return oss.str();
}

void ModuleLifecycleManager::Clear() {
    UnloadAll();
    factories_.clear();
    modules_.clear();
    init_order_.clear();
    dep_mgr_->Clear();
}
