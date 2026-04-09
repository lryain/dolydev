#pragma once
#include <stdint.h>

enum class ServoId
{
    SERVO_0,
    SERVO_1,
};

class ServoEventListener
{
public:
    virtual ~ServoEventListener() = default;
    virtual void onServoAbort(uint16_t id, ServoId channel) {}
    virtual void onServoError(uint16_t id, ServoId channel) {}
    virtual void onServoComplete(uint16_t id, ServoId channel) {}
};
