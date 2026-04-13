#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

/**
 * @class DependencyManager
 * @brief 管理模块间的依赖关系，检测循环依赖并生成初始化顺序
 * 
 * 功能：
 * 1. 注册模块及其依赖关系
 * 2. 检测循环依赖（使用DFS 3-state coloring）
 * 3. 生成初始化顺序（使用拓扑排序）
 * 4. 生成卸载顺序（依赖关系反序）
 * 5. 检查依赖满足情况
 */
class DependencyManager {
public:
    DependencyManager() = default;
    ~DependencyManager() = default;

    /**
     * 注册一个模块及其依赖
     * @param module_name 模块名
     * @param dependencies 依赖列表
     * @return 是否注册成功
     */
    bool RegisterModule(const std::string& module_name,
                       const std::vector<std::string>& dependencies = {});

    /**
     * 检查模块是否已注册
     * @param module_name 模块名
     * @return 是否已注册
     */
    bool HasModule(const std::string& module_name) const;

    /**
     * 获取模块的直接依赖
     * @param module_name 模块名
     * @return 依赖列表
     */
    std::vector<std::string> GetDependencies(const std::string& module_name) const;

    /**
     * 获取依赖于该模块的所有模块（反向依赖）
     * @param module_name 模块名
     * @return 依赖于该模块的模块列表
     */
    std::vector<std::string> GetDependents(const std::string& module_name) const;

    /**
     * 检查是否存在循环依赖
     * @return 是否有循环依赖
     */
    bool HasCyclicDependency() const;

    /**
     * 获取循环依赖链（如果存在）
     * @return 循环链中的模块列表，如果无循环则为空
     */
    std::vector<std::string> GetCyclicChain() const;

    /**
     * 获取初始化顺序（拓扑排序）
     * @return 模块初始化顺序
     */
    std::vector<std::string> GetInitializationOrder() const;

    /**
     * 获取卸载顺序（初始化顺序的反序）
     * @return 模块卸载顺序
     */
    std::vector<std::string> GetUnloadOrder() const;

    /**
     * 检查单个模块的依赖是否满足
     * @param module_name 模块名
     * @param available_modules 可用模块列表
     * @return 是否所有依赖都已满足
     */
    bool IsDependencySatisfied(const std::string& module_name,
                               const std::set<std::string>& available_modules) const;

    /**
     * 检查所有模块的依赖是否都满足
     * @param available_modules 可用模块列表
     * @return 是否所有依赖都已满足
     */
    bool AreAllDependenciesSatisfied(const std::set<std::string>& available_modules) const;

    /**
     * 获取所有已注册的模块
     * @return 模块名列表
     */
    std::vector<std::string> GetAllModules() const;

    /**
     * 获取依赖管理器的摘要信息
     * @return 摘要字符串
     */
    std::string GetSummary() const;

    /**
     * 清空所有依赖信息
     */
    void Clear();

private:
    // 私有结构
    struct ModuleInfo {
        std::vector<std::string> dependencies;  // 该模块的依赖
        std::set<std::string> dependents;       // 依赖于该模块的模块
    };

    // 数据成员
    std::map<std::string, ModuleInfo> modules_;

    // 私有方法
    /**
     * DFS检查循环依赖 (3-state coloring: white=0, gray=1, black=2)
     */
    bool HasCycle(const std::string& node, 
                  std::map<std::string, int>& colors,
                  std::vector<std::string>& path) const;

    /**
     * 拓扑排序 (Kahn算法)
     */
    std::vector<std::string> TopologicalSort() const;

    /**
     * 获取循环链中的所有节点
     */
    void GetCyclePath(const std::string& start_node,
                      std::vector<std::string>& path,
                      std::set<std::string>& visited) const;
};

/**
 * 工厂函数：创建DependencyManager
 */
inline std::shared_ptr<DependencyManager> CreateDependencyManager() {
    return std::make_shared<DependencyManager>();
}
