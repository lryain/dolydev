#include "vision/DependencyManager.h"
#include <algorithm>
#include <sstream>

bool DependencyManager::RegisterModule(const std::string& module_name,
                                      const std::vector<std::string>& dependencies) {
    if (module_name.empty()) return false;
    
    modules_[module_name].dependencies = dependencies;
    
    // 更新反向依赖（dependents）
    for (const auto& dep : dependencies) {
        modules_[dep].dependents.insert(module_name);
    }
    
    return true;
}

bool DependencyManager::HasModule(const std::string& module_name) const {
    return modules_.find(module_name) != modules_.end();
}

std::vector<std::string> DependencyManager::GetDependencies(const std::string& module_name) const {
    auto it = modules_.find(module_name);
    if (it != modules_.end()) {
        return it->second.dependencies;
    }
    return {};
}

std::vector<std::string> DependencyManager::GetDependents(const std::string& module_name) const {
    std::vector<std::string> result;
    auto it = modules_.find(module_name);
    if (it != modules_.end()) {
        for (const auto& dep : it->second.dependents) {
            result.push_back(dep);
        }
    }
    return result;
}

bool DependencyManager::HasCyclicDependency() const {
    std::map<std::string, int> colors;  // 0: white, 1: gray, 2: black
    for (const auto& [module, _] : modules_) {
        colors[module] = 0;
    }
    
    std::vector<std::string> path;
    for (const auto& [module, _] : modules_) {
        if (colors[module] == 0) {
            if (HasCycle(module, colors, path)) {
                return true;
            }
        }
    }
    
    return false;
}

std::vector<std::string> DependencyManager::GetCyclicChain() const {
    if (!HasCyclicDependency()) {
        return {};
    }
    
    std::map<std::string, int> colors;
    for (const auto& [module, _] : modules_) {
        colors[module] = 0;
    }
    
    std::vector<std::string> path;
    for (const auto& [module, _] : modules_) {
        if (colors[module] == 0) {
            if (HasCycle(module, colors, path)) {
                return path;
            }
        }
    }
    
    return {};
}

bool DependencyManager::HasCycle(const std::string& node,
                                 std::map<std::string, int>& colors,
                                 std::vector<std::string>& path) const {
    colors[node] = 1;  // gray
    path.push_back(node);
    
    auto it = modules_.find(node);
    if (it != modules_.end()) {
        for (const auto& dep : it->second.dependencies) {
            if (colors[dep] == 1) {
                // 找到回边，记录循环链
                path.push_back(dep);
                return true;
            }
            if (colors[dep] == 0) {
                if (HasCycle(dep, colors, path)) {
                    return true;
                }
            }
        }
    }
    
    path.pop_back();
    colors[node] = 2;  // black
    return false;
}

std::vector<std::string> DependencyManager::GetInitializationOrder() const {
    if (HasCyclicDependency()) {
        return {};
    }
    return TopologicalSort();
}

std::vector<std::string> DependencyManager::GetUnloadOrder() const {
    auto init_order = GetInitializationOrder();
    std::reverse(init_order.begin(), init_order.end());
    return init_order;
}

std::vector<std::string> DependencyManager::TopologicalSort() const {
    std::vector<std::string> result;
    std::map<std::string, int> in_degree;
    
    for (const auto& [module, _] : modules_) {
        in_degree[module] = 0;
    }
    
    for (const auto& [module, info] : modules_) {
        for (const auto& dep : info.dependencies) {
            in_degree[module]++;
        }
    }
    
    std::vector<std::string> queue;
    for (const auto& [module, degree] : in_degree) {
        if (degree == 0) {
            queue.push_back(module);
        }
    }
    
    while (!queue.empty()) {
        auto node = queue.front();
        queue.erase(queue.begin());
        result.push_back(node);
        
        // 处理所有依赖于当前节点的模块
        for (const auto& [module, info] : modules_) {
            auto it = std::find(info.dependencies.begin(), info.dependencies.end(), node);
            if (it != info.dependencies.end()) {
                in_degree[module]--;
                if (in_degree[module] == 0) {
                    queue.push_back(module);
                }
            }
        }
    }
    
    return result;
}

bool DependencyManager::IsDependencySatisfied(const std::string& module_name,
                                             const std::set<std::string>& available_modules) const {
    auto it = modules_.find(module_name);
    if (it == modules_.end()) {
        return false;
    }
    
    for (const auto& dep : it->second.dependencies) {
        if (available_modules.find(dep) == available_modules.end()) {
            return false;
        }
    }
    
    return true;
}

bool DependencyManager::AreAllDependenciesSatisfied(const std::set<std::string>& available_modules) const {
    for (const auto& [module, _] : modules_) {
        if (!IsDependencySatisfied(module, available_modules)) {
            return false;
        }
    }
    
    return true;
}

std::vector<std::string> DependencyManager::GetAllModules() const {
    std::vector<std::string> modules;
    for (const auto& [module, _] : modules_) {
        modules.push_back(module);
    }
    return modules;
}

std::string DependencyManager::GetSummary() const {
    std::ostringstream oss;
    oss << "DependencyManager Summary:\n";
    oss << "  Total modules: " << modules_.size() << "\n";
    oss << "  Modules:\n";
    
    for (const auto& [module, info] : modules_) {
        oss << "    " << module << " -> [";
        for (size_t i = 0; i < info.dependencies.size(); ++i) {
            oss << info.dependencies[i];
            if (i < info.dependencies.size() - 1) {
                oss << ", ";
            }
        }
        oss << "]\n";
    }
    
    oss << "  Cyclic dependency: " << (HasCyclicDependency() ? "YES" : "NO") << "\n";
    
    return oss.str();
}

void DependencyManager::Clear() {
    modules_.clear();
}