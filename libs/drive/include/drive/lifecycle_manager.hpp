/**
 * @file lifecycle_manager.hpp
 * @brief 生命周期管理（信号处理、优雅退出）
 */

#pragma once

#include <atomic>
#include <memory>

namespace doly::drive {

// 前向声明
class DriveService;

/**
 * @brief 生命周期管理器
 * 
 * 职责：
 * - 注册信号处理
 * - 实现优雅退出逻辑
 * - 管理全局运行状态
 */
class LifecycleManager {
public:
    static LifecycleManager& instance();
    
    // 初始化信号处理
    void init();
    
    // 设置关联的驱动服务（用于清理）
    void set_drive_service(std::shared_ptr<DriveService> service);
    
    // 获取全局运行状态
    bool is_running() const { return running_.load(); }
    
    // 请求关闭
    void request_shutdown();
    
    // 执行优雅退出
    void graceful_shutdown();
    
private:
    LifecycleManager() = default;
    ~LifecycleManager();
    
    std::atomic<bool> running_{true};
    std::atomic<bool> shutdown_in_progress_{false};
    std::shared_ptr<DriveService> drive_service_;
};

} // namespace doly::drive
