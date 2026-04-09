#pragma once
#include <cstdint>
#include "ArmEventListener.h"

namespace ArmEvent
{
    void AddListener(ArmEventListener* observer, bool priority = false);
    void RemoveListener(ArmEventListener* observer);
    void AddListenerOnComplete(void(*onEvent)(uint16_t id, ArmSide side));
    void RemoveListenerOnComplete(void(*onEvent)(uint16_t id, ArmSide side));
    void AddListenerOnError(void(*onEvent)(uint16_t id, ArmSide side, ArmErrorType errorType));
    void RemoveListenerOnError(void(*onEvent)(uint16_t id, ArmSide side, ArmErrorType errorType));
    void AddListenerOnStateChange(void(*onEvent)(ArmSide side, ArmState state));
    void RemoveListenerOnStateChange(void(*onEvent)(ArmSide side, ArmState state));
    void AddListenerOnMovement(void(*onEvent)(ArmSide side, float degreeChange));
    void RemoveListenerOnMovement(void(*onEvent)(ArmSide side, float degreeChange));
}
