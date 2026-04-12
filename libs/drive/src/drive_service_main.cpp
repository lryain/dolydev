/**
 * @file drive_service_main.cpp
 * @brief Drive Hardware Service Main Entry Point
 * 
 * 精简的主函数，所有初始化和控制逻辑已迁移到 DriveService 类
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/drive_service.hpp"
#include "drive/lifecycle_manager.hpp"
#include <iostream>
#include <getopt.h>
#include <cstring>
#include <unistd.h>

using namespace doly::drive;

void print_usage(const char* progname) {
    std::cout << "Usage: " << progname << " [OPTIONS]\n\n"
              << "ExtIO Hardware Service\n\n"
              << "Options:\n"
              << "  -c, --config FILE    Config file path (default: /home/pi/dev/.../config/pca9535.yaml)\n"
              << "  -d, --daemon         Run as daemon\n"
              << "  -v, --verbose        Verbose logging\n"
              << "  -h, --help           Show this help\n"
              << std::endl;
}

bool daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed" << std::endl;
        return false;
    }
    if (pid > 0) exit(0);
    
    if (setsid() < 0 || chdir("/") < 0) {
        return false;
    }
    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    return true;
}

int main(int argc, char* argv[]) {
    std::string config_file = "/home/pi/dolydev/config/pca9535.yaml";
    bool daemon_mode = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"daemon", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:dvh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'c': config_file = optarg; break;
            case 'd': daemon_mode = true; break;
            case 'v': verbose = true; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }
    
    if (daemon_mode && !daemonize()) {
        return 1;
    }
    
    LifecycleManager::instance().init();
    
    auto service = std::make_shared<DriveService>();
    LifecycleManager::instance().set_drive_service(service);
    
    if (!service->initialize(config_file)) {
        std::cerr << "Failed to initialize Drive Service" << std::endl;
        return 1;
    }
    
    std::cout << "[ExtIOService] Service running, press Ctrl+C to stop..." << std::endl;
    service->run();
    
    service->shutdown();
    return 0;
}
