import re

with open('/home/pi/dolydev/libs/drive/src/drive_service.cpp', 'r') as f:
    text = f.read()

# Add SDK commands to classify_command
motor_cond_old = '        action == "get_encoder_values") {'
motor_cond_new = '''        action == "get_encoder_values" ||
        action.find("sdk_") == 0) {'''

if motor_cond_old in text:
    text = text.replace(motor_cond_old, motor_cond_new)

# Add handling in execute_motor_command
handler_old = '    } else if (action == "get_encoder_values") {'
handler_new = '''    } else if (action == "sdk_init") {
        int gx = cmd.value("gx", 0);
        int gy = cmd.value("gy", 0);
        int gz = cmd.value("gz", 0);
        int ax = cmd.value("ax", 0);
        int ay = cmd.value("ay", 0);
        int az = cmd.value("az", 0);
        int8_t status = DriveControl::init(gx, gy, gz, ax, ay, az);
        std::cout << "[SDK] init() returned " << (int)status << std::endl;
    } else if (action == "sdk_dispose") {
        bool dispose_imu = cmd.value("dispose_imu", false);
        int8_t status = DriveControl::dispose(dispose_imu);
        std::cout << "[SDK] dispose() returned " << (int)status << std::endl;
    } else if (action == "sdk_is_active") {
        bool active = DriveControl::isActive();
        std::cout << "[SDK] isActive() returned " << (active ? "true" : "false") << std::endl;
    } else if (action == "sdk_abort") {
        DriveControl::Abort();
        std::cout << "[SDK] Abort() called" << std::endl;
    } else if (action == "sdk_free_drive") {
        uint8_t speed = cmd.value("speed", 50);
        bool isLeft = cmd.value("isLeft", true);
        bool toForward = cmd.value("toForward", true);
        bool accepted = DriveControl::freeDrive(speed, isLeft, toForward);
        std::cout << "[SDK] freeDrive() returned " << (accepted ? "true" : "false") << std::endl;
    } else if (action == "sdk_go_xy") {
        uint16_t id = cmd.value("id", 1000);
        int16_t x = cmd.value("x", 0);
        int16_t y = cmd.value("y", 0);
        uint8_t speed = cmd.value("speed", 50);
        bool toForward = cmd.value("toForward", true);
        bool with_brake = cmd.value("with_brake", false);
        uint8_t acc = cmd.value("accel_interval", 0);
        bool ctrl_speed = cmd.value("control_speed", false);
        bool ctrl_force = cmd.value("control_force", true);
        bool accpt = DriveControl::goXY(id, x, y, speed, toForward, with_brake, acc, ctrl_speed, ctrl_force);
        std::cout << "[SDK] goXY() returned " << (accpt ? "true" : "false") << std::endl;
    } else if (action == "sdk_go_distance") {
        uint16_t id = cmd.value("id", 1001);
        uint16_t mm = cmd.value("mm", 100);
        uint8_t speed = cmd.value("speed", 50);
        bool toForward = cmd.value("toForward", true);
        bool with_brake = cmd.value("with_brake", false);
        uint8_t acc = cmd.value("accel_interval", 0);
        bool ctrl_speed = cmd.value("control_speed", false);
        bool ctrl_force = cmd.value("control_force", true);
        bool accpt = DriveControl::goDistance(id, mm, speed, toForward, with_brake, acc, ctrl_speed, ctrl_force);
        std::cout << "[SDK] goDistance() returned " << (accpt ? "true" : "false") << std::endl;
    } else if (action == "sdk_go_rotate") {
        uint16_t id = cmd.value("id", 1002);
        float rotateAngle = cmd.value("rotateAngle", 90.0f);
        bool from_center = cmd.value("from_center", true);
        uint8_t speed = cmd.value("speed", 50);
        bool toForward = cmd.value("toForward", true);
        bool with_brake = cmd.value("with_brake", false);
        uint8_t acc = cmd.value("accel_interval", 0);
        bool ctrl_speed = cmd.value("control_speed", false);
        bool ctrl_force = cmd.value("control_force", true);
        bool accpt = DriveControl::goRotate(id, rotateAngle, from_center, speed, toForward, with_brake, acc, ctrl_speed, ctrl_force);
        std::cout << "[SDK] goRotate() returned " << (accpt ? "true" : "false") << std::endl;
    } else if (action == "sdk_get_position") {
        Position pos = DriveControl::getPosition();
        std::cout << "[SDK] getPosition() => diffx: " << pos.diffx << " diffy: " << pos.diffy << " theta: " << pos.theta << std::endl;
    } else if (action == "sdk_reset_position") {
        DriveControl::resetPosition();
        std::cout << "[SDK] resetPosition() called" << std::endl;
    } else if (action == "sdk_get_state") {
        DriveState st = DriveControl::getState();
        std::cout << "[SDK] getState() returned " << (int)st << std::endl;
    } else if (action == "sdk_get_rpm") {
        bool isLeft = cmd.value("isLeft", true);
        float rpm = DriveControl::getRPM(isLeft);
        std::cout << "[SDK] getRPM(" << isLeft << ") => " << rpm << std::endl;
    } else if (action == "sdk_get_version") {
        float ver = DriveControl::getVersion();
        std::cout << "[SDK] getVersion() => " << ver << std::endl;
    } else if (action == "get_encoder_values") {'''

if handler_old in text:
    text = text.replace(handler_old, handler_new)

with open('/home/pi/dolydev/libs/drive/src/drive_service.cpp', 'w') as f:
    f.write(text)

