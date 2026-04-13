#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <chrono>

// Forward declaration
class FrameContext;

/**
 * @enum ModuleState
 * @brief 模块的生命周期状态
 */
enum class ModuleState {
    Uninitialized = 0,  // 未初始化
    Initializing = 1,   // 初始化中
    Ready = 2,          // 就绪（已初始化但未运行）
    Running = 3,        // 运行中
    Paused = 4,         // 暂停
    Stopping = 5,       // 停止中
    Unloading = 6,      // 卸载中
    Error = 7           // 错误状态
};

/**
 * @class IModule
 * @brief 所有模块的抽象基类接口
 * 
 * 定义了模块的生命周期、处理接口、配置管理和状态查询
 */
class IModule {
public:
    virtual ~IModule() = default;

    // ==================== 生命周期方法 ====================
    
    /**
     * @brief 初始化模块
     * @param config 配置参数映射
     * @return 初始化是否成功
     */
    virtual bool Initialize(const std::map<std::string, std::string>& config) = 0;

    /**
     * @brief 启动模块（Initialize 后调用）
     * @return 启动是否成功
     */
    virtual bool Start() = 0;

    /**
     * @brief 处理一帧数据
     * @param frame_ctx 帧数据与元数据容器
     * @return 处理是否成功
     */
    virtual bool Process(FrameContext& frame_ctx) = 0;

    /**
     * @brief 停止模块（关闭前调用）
     * @return 停止是否成功
     */
    virtual bool Stop() = 0;

    /**
     * @brief 卸载模块（释放所有资源）
     * @return 卸载是否成功
     */
    virtual bool Unload() = 0;

    // ==================== 信息查询方法 ====================

    /**
     * @brief 获取模块名称
     * @return 模块唯一标识符
     */
    virtual std::string GetModuleName() const = 0;

    /**
     * @brief 获取模块版本
     * @return 版本字符串，格式如 "1.0.0"
     */
    virtual std::string GetVersion() const = 0;

    /**
     * @brief 获取该模块的依赖模块列表
     * @return 依赖的其他模块名称向量
     * 
     * 示例：FaceRecognition 模块依赖于 FaceDetection
     * 返回值：{"FaceDetection"}
     */
    virtual std::vector<std::string> GetDependencies() const = 0;

    /**
     * @brief 获取该模块提供的能力集合
     * @return 能力标识符列表
     * 
     * 示例：FaceDetection 模块提供 {"face_detection", "bbox_output"}
     */
    virtual std::vector<std::string> GetProvidedCapabilities() const = 0;

    // ==================== 配置与状态方法 ====================

    /**
     * @brief 动态更新模块配置（无需重启模块）
     * @param new_config 新的配置参数
     * @return 更新是否成功
     */
    virtual bool UpdateConfig(const std::map<std::string, std::string>& new_config) = 0;

    /**
     * @brief 获取当前模块状态
     * @return 模块的生命周期状态
     */
    virtual ModuleState GetState() const = 0;

    /**
     * @brief 获取最后一次错误信息
     * @return 错误信息字符串（无错误时返回空字符串）
     */
    virtual std::string GetLastError() const = 0;

    // ==================== 性能统计方法 ====================

    /**
     * @brief 获取平均处理时间
     * @return 毫秒为单位的平均处理时间
     */
    virtual double GetAverageProcessingTime() const = 0;

    /**
     * @brief 获取已处理的帧数
     * @return 帧计数
     */
    virtual long GetFramesProcessed() const = 0;

    /**
     * @brief 获取处理错误数
     * @return 错误计数
     */
    virtual int GetErrorCount() const = 0;

protected:
    // 派生类可使用的成员变量
    ModuleState state_ = ModuleState::Uninitialized;
    std::string last_error_;
    double avg_processing_time_ms_ = 0.0;
    long frames_processed_ = 0;
    int error_count_ = 0;
    std::chrono::steady_clock::time_point start_time_;
};

// ==================== 模块工厂函数接口 ====================

/**
 * @brief 模块工厂函数签名
 * 每个 .so 模块导出的创建函数应符合此签名
 */
extern "C" {
    typedef IModule* (*ModuleFactory)();
    typedef void (*ModuleDestructor)(IModule*);
}
