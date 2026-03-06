/**
 * @file motor_test_with_encoder.cpp
 * @brief 参考 motor_test_with_encoder.py 的 C++ 版本电机带编码器跟踪测试脚本
 *
 * 功能:
 *   - 实时监测编码器脉冲
 *   - 支持 --pulses N 驱动电机前进/后退 N 个脉冲
 *   - 支持 --throttle 指定驱动速度
 *   - 支持 --assume-rate 指定编码器速率用于时长估算
 *   - 支持 --do-enc-test 自动化测试（前进500，后退-500）
 *   - 支持 --yes 无需人工确认直接执行
 *   - 支持 --dry-run 仅监测编码器不驱动电机
 *   - 支持 --report-out 输出 JSON 测试结果
 *   - 支持 --quiet 抑制实时编码器输出
 *
 * 编译:
 *   cd /home/pi/dolydev/libs/motor_control_cpp
 *   mkdir -p build && cd build
 *   cmake ..
 *   make motor_test_with_encoder -j4
 *
 * 使用示例:
 *   ./motor_test_with_encoder --pulses 50 --throttle 0.2 --assume-rate 100
 *   ./motor_test_with_encoder --do-enc-test --yes --report-out result.json
 *   ./motor_test_with_encoder --dry-run  # 仅监测编码器
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>

#include "drive/motor_controller.hpp"

// JSON 输出辅助（使用 nlohmann/json 如果可用，否则简单实现）
#ifndef NLOHMANN_JSON_HPP
// 简单 JSON 输出实现
#include <map>
#include <vector>
class SimpleJSON {
public:
    std::map<std::string, std::string> data;
    void set(const std::string& key, const std::string& value) { data[key] = value; }
    void set(const std::string& key, bool value) { data[key] = value ? "true" : "false"; }
    void set(const std::string& key, long value) { data[key] = std::to_string(value); }
    std::string dump(int indent = 2) {
        std::string result = "{\n";
        for (auto it = data.begin(); it != data.end(); ++it) {
            result += std::string(indent, ' ') + "\"" + it->first + "\": \"" + it->second + "\"";
            if (std::next(it) != data.end()) result += ",";
            result += "\n";
        }
        result += "}";
        return result;
    }
};
using json = SimpleJSON;
#else
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#endif

static std::atomic<bool> running(true);

void sigint_handler(int) {
    running = false;
}

/**
 * @brief 估算并执行脉冲移动，实时跟踪编码器
 * @param controller 电机控制器对象
 * @param pulses 目标脉冲数（正=前进，负=后退）
 * @param assume_rate 编码器速率（脉冲/秒）用于估算时长
 * @param throttle 驱动速度（0.0~1.0）
 * @param quiet 是否抑制实时输出
 * @return {reached, {left_pulses, right_pulses}} reached 表示是否在预期时间内到达目标
 */
struct MovePulsesResult {
    bool reached;
    long left_pulses;
    long right_pulses;
};

MovePulsesResult move_pulses(MotorController& controller, long pulses,
                           double assume_rate = 100.0, float throttle = 0.5f,
                           bool quiet = false) {
    if (assume_rate <= 0.0) {
        std::cerr << "ERROR: 无效的假定速率 assume_rate=" << assume_rate << std::endl;
        return {false, 0, 0};
    }

    long target = pulses;
    int sign = (target >= 0) ? 1 : -1;
    double needed = std::abs((double)target) / assume_rate;  // 估算所需时长（秒）

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "移动目标: " << target << " 脉冲，估算时间 " << needed << "s （throttle="
              << throttle << "）" << std::endl;

    // 获取初始位置
    long init_left = controller.getLeftEncoderPosition();
    long init_right = controller.getRightEncoderPosition();

    // 启动电机
    float left_val = throttle * sign;
    float right_val = throttle * sign;
    float duration_for_controller = std::max(static_cast<float>(needed), 0.5f);

    std::cout << "[motor] setSpeeds(left=" << left_val << ", right=" << right_val
              << ", duration=" << duration_for_controller << ")" << std::endl;

    try {
        controller.setSpeeds(left_val, right_val, duration_for_controller);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } catch (const std::exception& e) {
        std::cerr << "[motor] setSpeeds failed: " << e.what() << std::endl;
        return {false, 0, 0};
    }

    auto start = std::chrono::steady_clock::now();
    bool reached = false;
    long final_left = 0, final_right = 0;

    try {
        while (running) {
            long left = controller.getLeftEncoderPosition();
            long right = controller.getRightEncoderPosition();
            long delta_left = left - init_left;
            long delta_right = right - init_right;

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();

            if (!quiet) {
                std::cout << "t=" << std::fixed << std::setprecision(2) << elapsed << "s"
                          << " L=" << delta_left << " R=" << delta_right << std::endl;
            }

            // 判定是否到达目标（使用绝对值）
            if (std::abs(delta_left) >= std::abs(target) &&
                std::abs(delta_right) >= std::abs(target)) {
                reached = true;
                final_left = delta_left;
                final_right = delta_right;
                break;
            }

            // 超时保护：3倍预期时长 + 1秒
            if (elapsed > needed * 3.0 + 1.0) {
                std::cout << "移动超时，停止" << std::endl;
                final_left = delta_left;
                final_right = delta_right;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR during movement: " << e.what() << std::endl;
    }

    // 停止电机
    try {
        controller.stop();
    } catch (const std::exception& e) {
        std::cerr << "WARNING: stop() failed: " << e.what() << std::endl;
    }

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "移动结束, reached=" << (reached ? "true" : "false")
              << " snapshot L=" << final_left << " R=" << final_right << std::endl;

    return {reached, final_left, final_right};
}

/**
 * @brief 打印用法帮助
 */
void print_help(const char* prog) {
    std::cout << "用法: " << prog << " [选项]\n"
              << "选项:\n"
              << "  --quiet              不打印实时编码器输出\n"
              << "  --pulses N           让电机前进(正) 或后退(负) 指定脉冲数\n"
              << "  --throttle FLOAT     指定 throttle (0.0~1.0, 默认 0.5)\n"
              << "  --assume-rate FLOAT  假定的编码器速率 (脉冲/s, 默认 100.0)\n"
              << "  --do-enc-test        执行自动化测试：前进500，后退-500\n"
              << "  --yes                无需确认直接执行（谨慎使用）\n"
              << "  --dry-run            仅监测编码器不驱动电机\n"
              << "  --report-out FILE    将测试结果输出为 JSON 文件\n"
              << "  --use-pid            启用电机 PID（默认关闭，需正确的 PID 参数与速度反馈）\n"
              << "  --help               显示此帮助信息\n";
}

int main(int argc, char** argv) {
    std::signal(SIGINT, sigint_handler);

    // 参数解析（对标 Python argparse）
    bool quiet = false;
    long pulses = 0;
    float throttle = -1.0f;  // -1 表示未指定，使用默认 0.5
    double assume_rate = 100.0;
    bool do_enc_test = false;
    bool yes_flag = false;
    bool dry_run = false;
    bool use_pid = false;
    float cli_ramp_time = -1.0f;
    std::string report_out;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--pulses" && i + 1 < argc) {
            pulses = std::stol(argv[++i]);
        } else if (arg == "--throttle" && i + 1 < argc) {
            throttle = std::stof(argv[++i]);
        } else if (arg == "--assume-rate" && i + 1 < argc) {
            assume_rate = std::stod(argv[++i]);
        } else if (arg == "--do-enc-test") {
            do_enc_test = true;
        } else if (arg == "--yes") {
            yes_flag = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--report-out" && i + 1 < argc) {
            report_out = argv[++i];
        } else if (arg == "--use-pid") {
            use_pid = true;
        } else if (arg == "--ramp-time" && i + 1 < argc) {
            cli_ramp_time = std::stof(argv[++i]);
        }
    }

    // 使用默认 throttle
    if (throttle < 0.0f) {
        throttle = 0.5f;
    }

    std::cout << "=== 电机带编码器跟踪测试脚本 (C++ 版本) ===" << std::endl;

    // 初始化电机控制器（对标 Python 的 open_motors()）
    MotorController controller;
    if (!controller.init()) {
        std::cerr << "ERROR: 电机控制器初始化失败" << std::endl;
        return 2;
    }

    // 如果命令行指定了 ramp time，则覆盖实例值并打印日志
    if (cli_ramp_time > 0.0f) {
        controller.setRampTime(cli_ramp_time);
        std::cout << "⚙️ CLI: ramp_time 已设置为 " << cli_ramp_time << " 秒 SRC: [--ramp-time]" << std::endl;
    }

    // 与 Python 版本一致：初始化时启用编码器，禁用 PID
    try {
        controller.enableEncoders(true);
        controller.enablePID(use_pid);
        if (!use_pid) {
            controller.enablePID(false);
        }
    } catch (const std::exception& e) {
        // 忽略错误，继续执行
    }

    if (use_pid) {
        std::cout << "⚙️ 已启用 PID 模式（请确保 PID 参数与编码器速度反馈正确配置）" << std::endl;
    }

    auto ensure_encoders_ready = [&controller]() -> bool {
        if (controller.hasLeftEncoder() && controller.hasRightEncoder()) {
            return true;
        }

        std::cerr << "⚠️ 初次启用编码器检测到通道缺失，尝试重新初始化..." << std::endl;
        try {
            controller.enableEncoders(false);
        } catch (const std::exception& e) {
            std::cerr << "重新禁用编码器失败: " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        try {
            controller.enableEncoders(true);
        } catch (const std::exception& e) {
            std::cerr << "重新启用编码器失败: " << e.what() << std::endl;
        }

        return controller.hasLeftEncoder() && controller.hasRightEncoder();
    };

    if (!ensure_encoders_ready()) {
        std::cerr << "❌ 编码器未就绪: 左=" << (controller.hasLeftEncoder() ? "OK" : "未启用")
                  << " 右=" << (controller.hasRightEncoder() ? "OK" : "未启用") << std::endl;
        std::cerr << "请确认没有其他进程占用编码器 GPIO（例如 Python 测试脚本），并确保 ID_SD/ID_SC 引脚未被 I2C 驱动占用。" << std::endl;
        return 6;
    }

    std::cout << "✓ 电机控制器已初始化" << std::endl;

    // dry-run 模式下禁用电机驱动（通过不调用 setSpeeds 实现）
    if (dry_run) {
        std::cout << "--dry-run 已开启：将仅监测编码器，不驱动电机" << std::endl;
    }

    try {
        // 自动化编码器正反向验证：前进 +500，再后退 -500
        if (do_enc_test) {
            std::cout << "\n开始自动化编码器正反向测试（目标 +500 / -500）" << std::endl;

            // 确认驱动电机
            if (!yes_flag && !dry_run) {
                std::cout << "将要驱动电机进行自动化测试，请确认人员与环境安全" << std::endl;
                std::cout << "输入大写 Y 开始，其他键取消: ";
                std::string resp;
                std::getline(std::cin, resp);
                if (resp != "Y") {
                    std::cout << "未确认，取消测试" << std::endl;
                    if (!report_out.empty()) {
                        json result;
                        result.set("status", "cancelled");
                        std::ofstream ofs(report_out);
                        ofs << result.dump(2) << std::endl;
                    }
                    return 5;
                }
            }

            if (dry_run) {
                std::cout << "⚠️ --dry-run 模式，不能执行 --do-enc-test" << std::endl;
                if (!report_out.empty()) {
                    json result;
                    result.set("status", "dry_run_mode");
                    std::ofstream ofs(report_out);
                    ofs << result.dump(2) << std::endl;
                }
                return 4;
            }

            bool passed = true;
            json result;
            result.set("test_type", "automated_enc_test");

            // 正向移动
            std::cout << "\n>>> 正向移动到 +500 脉冲" << std::endl;
            auto res_fwd = move_pulses(controller, 500, assume_rate, throttle, quiet);
            result.set("fwd_reached", res_fwd.reached);
            result.set("fwd_left", std::to_string(res_fwd.left_pulses));
            result.set("fwd_right", std::to_string(res_fwd.right_pulses));

            if (!res_fwd.reached) {
                std::cout << "❌ 未能在预期时间内达到正向 500 脉冲" << std::endl;
                passed = false;
            } else {
                std::cout << "✓ 正向到达 快照: L=" << res_fwd.left_pulses
                          << " R=" << res_fwd.right_pulses << std::endl;
            }

            // 短暂停顿
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // 反向移动
            std::cout << "\n>>> 反向移动到 -500 脉冲" << std::endl;
            auto res_bwd = move_pulses(controller, -500, assume_rate, throttle, quiet);
            result.set("bwd_reached", res_bwd.reached);
            result.set("bwd_left", std::to_string(res_bwd.left_pulses));
            result.set("bwd_right", std::to_string(res_bwd.right_pulses));

            if (!res_bwd.reached) {
                std::cout << "❌ 未能在预期时间内达到反向 -500 脉冲" << std::endl;
                passed = false;
            } else {
                std::cout << "✓ 反向到达 快照: L=" << res_bwd.left_pulses
                          << " R=" << res_bwd.right_pulses << std::endl;
            }

            result.set("passed", passed);

            if (passed) {
                std::cout << "\n✓✓✓ 自动化编码器测试通过 ✓✓✓" << std::endl;
                if (!report_out.empty()) {
                    std::ofstream ofs(report_out);
                    ofs << result.dump(2) << std::endl;
                    std::cout << "测试结果已保存到: " << report_out << std::endl;
                }
                return 0;
            } else {
                std::cout << "\n❌ 自动化编码器测试失败" << std::endl;
                if (!report_out.empty()) {
                    std::ofstream ofs(report_out);
                    ofs << result.dump(2) << std::endl;
                    std::cout << "测试结果已保存到: " << report_out << std::endl;
                }
                return 3;
            }
        }

        // 指定脉冲数的驱动
        if (pulses != 0) {
            if (!dry_run) {
                std::cout << "\n>>> 启动电机驱动" << std::endl;
                move_pulses(controller, pulses, assume_rate, throttle, quiet);
            } else {
                std::cout << "--dry-run: 跳过电机驱动" << std::endl;
            }
        } else {
            // 实时监测模式（对标 Python 的实时显示）
            std::cout << "\n未指定 --pulses，进入实时显示模式，按 Ctrl+C 结束" << std::endl;
            long last_left = controller.getLeftEncoderPosition();
            long last_right = controller.getRightEncoderPosition();

            while (running) {
                long left = controller.getLeftEncoderPosition();
                long right = controller.getRightEncoderPosition();
                long delta_left = left - last_left;
                long delta_right = right - last_right;

                if (!quiet) {
                    std::cout << "L=" << left << " (Δ=" << delta_left
                              << ") R=" << right << " (Δ=" << delta_right << ")" << std::endl;
                }

                last_left = left;
                last_right = right;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            std::cout << "退出监测模式" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    }

    // 清理资源
    std::cout << "\n关闭: 停止电机" << std::endl;
    try {
        controller.stop();
    } catch (const std::exception& e) {
        std::cerr << "WARNING during cleanup: " << e.what() << std::endl;
    }

    return 0;
}