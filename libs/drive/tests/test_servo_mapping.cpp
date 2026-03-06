/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/servo_controller.hpp"
#include <cassert>
#include <iostream>

// 纯逻辑单元测试：验证角度映射与边界不依赖硬件
int main() {
    doly::drive::ServoController ctrl;

    // 左右通道默认参数一致，只测一个通道
    float a0 = ctrl.LogicalToPhysical(SERVO_LEFT, 0.0f);
    float a90 = ctrl.LogicalToPhysical(SERVO_LEFT, 90.0f);
    float a180 = ctrl.LogicalToPhysical(SERVO_LEFT, 180.0f);

    assert(a0 >= 0.0f && "0 度映射需大于等于 0");
    assert(a180 <= SERVO_ARM_MAX_ANGLE && "180 度映射不能超过最大物理角");
    assert(a90 > a0 && a90 < a180 && "90 度应落在 0 与最大角之间");

    // 超界输入应被 clamp
    float aneg = ctrl.LogicalToPhysical(SERVO_LEFT, -30.0f);
    float aover = ctrl.LogicalToPhysical(SERVO_LEFT, 999.0f);
    assert(aneg == a0 && "负角度应被夹到 0" );
    assert(aover == a180 && "超出角度应被夹到最大" );
    (void)aneg; // 静态检查消除未使用告警
    (void)aover;

    std::cout << "[test_servo_mapping] PASS: 0->" << a0
              << " 90->" << a90
              << " 180->" << a180 << std::endl;
    return 0;
}
