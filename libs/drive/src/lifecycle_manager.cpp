/**
 * @file lifecycle_manager.cpp
 * @brief 生命周期管理实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/lifecycle_manager.hpp"
#include "drive/drive_service.hpp"
#include <signal.h>
#include <iostream>
#include <cstring>

#define LOG_PREFIX "[ExtIOService]"

namespace doly::drive {

// static LifecycleManager* g_lifecycle_instance = nullptr;

static void signal_handler(int signum) {
    std::cout << LOG_PREFIX << " Received signal " << signum 
              << " (" << strsignal(signum) << "), shutting down..." << std::endl;
    LifecycleManager::instance().request_shutdown();
}

LifecycleManager& LifecycleManager::instance() {
    static LifecycleManager instance;
    return instance;
}

LifecycleManager::~LifecycleManager() {
    if (shutdown_in_progress_.load()) {
        return;
    }
    graceful_shutdown();
}

void LifecycleManager::init() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
}

void LifecycleManager::set_drive_service(std::shared_ptr<DriveService> service) {
    drive_service_ = service;
}

void LifecycleManager::request_shutdown() {
    running_ = false;
}

void LifecycleManager::graceful_shutdown() {
    if (shutdown_in_progress_.exchange(true)) {
        return;
    }
    
    running_ = false;
    
    std::cout << LOG_PREFIX << " ======================================" << std::endl;
    std::cout << LOG_PREFIX << " Shutting down ExtIO Service..." << std::endl;
    
    if (drive_service_) {
        drive_service_->shutdown();
    }
    
    std::cout << LOG_PREFIX << " ExtIO Service stopped gracefully" << std::endl;
    std::cout << LOG_PREFIX << " ======================================" << std::endl;
}

} // namespace doly::drive
