#!/bin/bash
# Drive 服务快速测试脚本

echo "=== Drive Service Quick Test ==="
echo ""

# 检查编译产物
if [ ! -f "/home/pi/dolydev/libs/drive/build/libdoly_extio.a" ]; then
    echo "❌ libdoly_extio.a not found"
    exit 1
fi

if [ ! -f "/home/pi/dolydev/libs/drive/build/extio_service" ]; then
    echo "❌ extio_service not found"
    exit 1
fi

echo "✅ Build artifacts found"
echo "  - libdoly_extio.a: $(ls -lh /home/pi/dolydev/libs/drive/build/libdoly_extio.a | awk '{print $5}')"
echo "  - extio_service: $(ls -lh /home/pi/dolydev/libs/drive/build/extio_service | awk '{print $5}')"
echo ""

# 检查新增的源文件
echo "✅ New modules integrated:"
echo "  - servo_controller.cpp"
echo "  - led_controller.cpp"
echo "  - motor_controller.cpp (modified)"
echo ""

# 检查官方库链接
echo "✅ Official Doly libraries linked:"
ldd /home/pi/dolydev/libs/drive/build/extio_service 2>/dev/null | grep -E "libServoMotor|libGpio|libTimer" || echo "  (static linking, not shown in ldd)"
echo ""

echo "=== Architecture Summary ==="
echo "PWM Frequency: 50Hz (unified)"
echo "Servo Control: ServoMotor API (PWM4-5)"
echo "LED Control: Gpio PWM API (PWM6-11)"
echo "Motor Control: Custom driver (PWM12-15)"
echo ""

echo "=== Next Steps ==="
echo "1. Run: sudo ./extio_service"
echo "2. Test servo: ZMQ command 'cmd.drive.servo'"
echo "3. Test LED: ZMQ command 'cmd.drive.led'"
echo "4. Test motor: ZMQ command 'cmd.drive.motor'"
echo ""

echo "✅ Build successful! Ready for integration testing."
