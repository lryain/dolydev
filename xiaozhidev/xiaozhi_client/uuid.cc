#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <random>
#include <sstream>
#include <iomanip>

// Include nlohmann/json library
#include "json.hpp"

using json = nlohmann::json;

/**
 * 获取无线网卡的 MAC 地址
 *
 * 该函数遍历 /sys/class/net/ 目录下的所有网络接口，
 * 查找名称以 "wlan" 或 "wlp" 开头的无线网卡接口，
 * 并读取其对应的 address 文件以获取 MAC 地址。
 *
 * @return 无线网卡的 MAC 地址，如果未找到则返回空字符串
 */
std::string get_wireless_mac_address() {
    DIR *dir;
    struct dirent *entry;
    std::string mac_address;

    // 打开 /sys/class/net/ 目录
    dir = opendir("/sys/class/net/");
    if (dir == nullptr) {
        std::cerr << "Failed to open /sys/class/net/ directory" << std::endl;
        return "";
    }

    // 遍历目录中的所有条目
    while ((entry = readdir(dir)) != nullptr) {
        std::string interface_name = entry->d_name;

        // 检查接口名称是否以 wlan 或 wlp 开头
        if (interface_name.find("wlan") == 0 || interface_name.find("wlp") == 0) {
            std::string address_path = "/sys/class/net/" + interface_name + "/address";

            // 打开 address 文件
            std::ifstream address_file(address_path);
            if (address_file.is_open()) {
                std::getline(address_file, mac_address);
                address_file.close();
                closedir(dir);
                return mac_address;
            }
        }
    }

    closedir(dir);
    return "";
}

/**
 * 生成 UUID
 *
 * 该函数使用 std::random_device 和 std::mt19937 生成一个随机的 UUID。
 * UUID 的格式为 8-4-4-4-12 的 32 位十六进制数字。
 *
 * @return 生成的 UUID 字符串
 */
std::string generate_uuid() {
    // 使用静态变量确保 random_device 和 mt19937 只被初始化一次
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;

    ss << std::hex;

    // 生成 UUID 的各个部分
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    };

    return ss.str();
}

#if 0
int main() {
    // 获取无线网卡的 MAC 地址
    std::string wireless_mac = get_wireless_mac_address();
    if (!wireless_mac.empty()) {
        std::cout << "Wireless MAC Address: " << wireless_mac << std::endl;
    } else {
        std::cerr << "Failed to get wireless MAC address" << std::endl;
    }

    // 生成多个 UUID 并打印
    for (int i = 0; i < 5; ++i) {
        std::string uuid = generate_uuid();
        std::cout << "Generated UUID " << i + 1 << ": " << uuid << std::endl;
    }

    return 0;
}
#endif