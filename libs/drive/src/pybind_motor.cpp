/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include <pybind11/pybind11.h>
#include "motor_controller.hpp"
#include "safety_monitor.hpp"
#include "encoder_reader.hpp"

namespace py = pybind11;

PYBIND11_MODULE(motor_controller_cpp, m) {
    py::class_<MotorController>(m, "MotorController")
        .def(py::init<const std::string&, int>(),
             py::arg("i2c_dev") = "/dev/i2c-3",
             py::arg("addr") = 0x40)
        .def("init", &MotorController::init)
        .def("setSpeeds", &MotorController::setSpeeds,
             py::arg("left"), py::arg("right"), py::arg("duration") = -1.0f)
        .def("stop", &MotorController::stop)
        .def("forward", &MotorController::forward,
             py::arg("speed") = 0.5f, py::arg("duration") = -1.0f)
        .def("backward", &MotorController::backward,
             py::arg("speed") = 0.5f, py::arg("duration") = -1.0f)
        .def("turnLeft", &MotorController::turnLeft,
             py::arg("speed") = 0.5f, py::arg("duration") = -1.0f)
        .def("turnRight", &MotorController::turnRight,
             py::arg("speed") = 0.5f, py::arg("duration") = -1.0f)
        .def("setContinuousMode", &MotorController::setContinuousMode)
        .def("setAutoStopTimeout", &MotorController::setAutoStopTimeout)
        .def("getLeftSpeed", &MotorController::getLeftSpeed)
        .def("getRightSpeed", &MotorController::getRightSpeed)
        .def("isContinuousMode", &MotorController::isContinuousMode)
        .def("enablePID", &MotorController::enablePID)
        .def("setPIDParameters", &MotorController::setPIDParameters)
        .def("updateEncoderFeedback", &MotorController::updateEncoderFeedback)
        .def("isPIDEnabled", &MotorController::isPIDEnabled)
        .def("enableEncoders", &MotorController::enableEncoders)
        .def("isEncodersEnabled", &MotorController::isEncodersEnabled)
        .def("enableSafety", &MotorController::enableSafety)
        .def("isSafe", &MotorController::isSafe)
        .def("getLeftEncoderPosition", &MotorController::getLeftEncoderPosition)
        .def("getRightEncoderPosition", &MotorController::getRightEncoderPosition)
        .def("getLeftEncoderDelta", &MotorController::getLeftEncoderDelta)
        .def("getRightEncoderDelta", &MotorController::getRightEncoderDelta)
        .def("setPWM", &MotorController::setPWM,
             py::arg("channel"), py::arg("on"), py::arg("off"))
        .def("setEncoderDebugEnabled", &MotorController::setEncoderDebugEnabled,
             py::arg("enabled"));

    py::class_<EncoderReader>(m, "EncoderReader")
        .def(py::init<int, int, const std::string&>(),
             py::arg("gpio_a_pin"), py::arg("gpio_b_pin"), py::arg("consumer") = "encoder")
        .def("init", &EncoderReader::init)
        .def("start", &EncoderReader::start)
        .def("stop", &EncoderReader::stop)
        .def("getPosition", &EncoderReader::getPosition)
        .def("getDeltaPosition", &EncoderReader::getDeltaPosition)
        .def("getVelocity", &EncoderReader::getVelocity)
        .def("setDebugEnabled", &EncoderReader::setDebugEnabled,
             py::arg("enabled"));

     py::class_<SafetyMonitor>(m, "SafetyMonitor")
          // SafetyMonitor only provides a default constructor in C++
          .def(py::init<>())
        .def("init", &SafetyMonitor::init)
        .def("start", &SafetyMonitor::start)
        .def("stop", &SafetyMonitor::stop)
        .def("setCurrentLimit", &SafetyMonitor::setCurrentLimit)
        .def("setVoltageLimits", &SafetyMonitor::setVoltageLimits)
        .def("setTemperatureLimit", &SafetyMonitor::setTemperatureLimit)
        .def("getCurrent", &SafetyMonitor::getCurrent)
        .def("getVoltage", &SafetyMonitor::getVoltage)
        .def("getTemperature", &SafetyMonitor::getTemperature)
        .def("isOvercurrent", &SafetyMonitor::isOvercurrent)
        .def("isOvervoltage", &SafetyMonitor::isOvervoltage)
        .def("isUndervoltage", &SafetyMonitor::isUndervoltage)
        .def("isOvertemperature", &SafetyMonitor::isOvertemperature)
        .def("isSafe", &SafetyMonitor::isSafe)
        .def("setSafetyCallback", &SafetyMonitor::setSafetyCallback);

     // 静态方法绑定：返回 (left_reverse, right_reverse)
     m.def("loadMotorConfig", [](const std::string& config_file) {
          bool left = false, right = false;
          MotorController::loadMotorConfig(left, right, config_file);
          return py::make_tuple(left, right);
     }, py::arg("config_file") = "motor_config.ini");
}
