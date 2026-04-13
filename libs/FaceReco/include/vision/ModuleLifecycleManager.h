#pragma once

#include "IModule.h"
#include "FrameContext.h"
#include "ConfigManager.h"
#include "DependencyManager.h"
#include <map>
#include <memory>
#include <functional>

// 类型定义
using DependencyManagerPtr = std::shared_ptr<DependencyManager>;

/**
 * @class ModuleLifecycleManager
 * @brief 管理模块的完整生命周期
 * 
 * 功能：
 * - 模块注册和加载
 * - 按依赖顺序初始化
 * - 统一启动/停止
 * - 错误处理和恢复
 */
class ModuleLifecycleManager {
public:
    // ==================== 构造和析构 ====================
    
    ModuleLifecycleManager();
    ~ModuleLifecycleManager();
    
    ModuleLifecycleManager(const ModuleLifecycleManager&) = delete;
    ModuleLifecycleManager& operator=(const ModuleLifecycleManager&) = delete;

    // ==================== 模块管理 ====================

    /**
     * @brief 注册模块工厂函数
     * @param module_name 模块名
     * @param factory 创建模块的工厂函数
     * @param dependencies 模块依赖列表
     * @return 注册是否成功
     */
    bool RegisterModule(
        const std::string& module_name,
        std::function<std::shared_ptr<IModule>()> factory,
        const std::vector<std::string>& dependencies = {});

    /**
     * @brief 检查模块是否已注册
     * @param module_name 模块名
     * @return 是否已注册
     */
    bool HasModule(const std::string& module_name) const;

    /**
     * @brief 获取模块实例
     * @param module_name 模块名
     * @return 模块指针（可能为 nullptr）
     */
    std::shared_ptr<IModule> GetModule(const std::string& module_name) const;

    // ==================== 生命周期 ====================

    /**
     * @brief 初始化所有模块
     * @param config 配置管理器
     * @return 初始化是否成功
     */
    bool InitializeAll(ConfigManagerPtr config);

    /**
     * @brief 初始化指定模块集合（会自动补齐依赖，并按依赖顺序初始化）。
     * @param config 配置管理器
     * @param selected_modules 需要初始化的模块名集合
     */
    bool InitializeSelected(ConfigManagerPtr config, const std::vector<std::string>& selected_modules);

    /**
     * @brief 启动所有模块
     * @return 启动是否成功
     */
    bool StartAll();

    /**
     * @brief 启动指定模块集合（仅对已加载模块生效，按 init_order_ 顺序）。
     */
    bool StartSelected(const std::vector<std::string>& selected_modules);

    /**
     * @brief 处理帧（执行所有模块）
     * @param frame 帧数据
     * @return 处理是否成功
     */
    bool ProcessFrame(FrameContextPtr frame);

    /**
     * @brief 停止所有模块
     * @return 停止是否成功
     */
    bool StopAll();

    /**
     * @brief 停止指定模块集合（仅对已加载模块生效，按 init_order_ 逆序）。
     */
    bool StopSelected(const std::vector<std::string>& selected_modules);

    /**
     * @brief 卸载所有模块
     * @return 卸载是否成功
     */
    bool UnloadAll();

    /**
     * @brief 卸载指定模块集合（会按依赖逆序卸载，且仅卸载本次集合闭包内的模块）。
     */
    bool UnloadSelected(const std::vector<std::string>& selected_modules);

    // ==================== 状态查询 ====================

    /**
     * @brief 获取所有已加载的模块列表
     * @return 模块列表
     */
    std::vector<std::string> GetLoadedModules() const;

    /**
     * @brief 获取所有注册的模块列表
     * @return 模块列表
     */
    std::vector<std::string> GetRegisteredModules() const;

    /**
     * @brief 获取系统摘要
     * @return 摘要字符串
     */
    std::string GetSummary() const;

    /**
     * @brief 清空所有模块
     */
    void Clear();

private:
    // 工厂函数存储
    std::map<std::string, std::function<std::shared_ptr<IModule>()>> factories_;
    
    // 已加载的模块实例
    std::map<std::string, std::shared_ptr<IModule>> modules_;
    
    // 依赖管理器
    DependencyManagerPtr dep_mgr_;
    
    // 初始化顺序
    std::vector<std::string> init_order_;

    // ==================== 辅助方法 ====================

    /**
     * @brief 检查并加载模块
     */
    bool LoadModule(const std::string& module_name);

    /**
     * @brief 执行模块处理
     */
    bool ExecuteModuleProcessing(const std::string& module_name, FrameContextPtr frame);

    /**
     * @brief 计算 selected_modules + 依赖 的闭包集合
     */
    std::vector<std::string> ResolveDependencyClosure(const std::vector<std::string>& selected_modules) const;
};

/**
 * @typedef ModuleLifecycleManagerPtr
 * @brief ModuleLifecycleManager 智能指针
 */
using ModuleLifecycleManagerPtr = std::shared_ptr<ModuleLifecycleManager>;

/**
 * @brief 创建 ModuleLifecycleManager 实例
 * @return 新的 ModuleLifecycleManager
 */
inline ModuleLifecycleManagerPtr CreateModuleLifecycleManager() {
    return std::make_shared<ModuleLifecycleManager>();
}
