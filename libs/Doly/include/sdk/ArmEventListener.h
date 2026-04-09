#pragma once
#include <cstdint>

enum class ArmErrorType : uint8_t
{
    ABORT,
    MOTOR,
};

enum class ArmSide : uint8_t
{
    BOTH,
    LEFT,
    RIGHT,
};

enum class ArmState : uint8_t
{
    RUNNING,
    COMPLETED,
    ERROR,
};

class ArmEventListener
{
public:
    virtual ~ArmEventListener() = default;
    virtual void onArmComplete(uint16_t id, ArmSide side) {}
    virtual void onArmError(uint16_t id, ArmSide side, ArmErrorType errorType) {}
    virtual void onArmStateChange(ArmSide side, ArmState state) {}
    virtual void onArmMovement(ArmSide side, float degreeChange) {}
};
