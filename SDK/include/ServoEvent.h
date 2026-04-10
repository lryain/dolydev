#pragma once
#include <stdint.h>
#include "ServoEventListener.h"

namespace ServoEvent
{
    void AddListener(ServoEventListener* observer, bool priority = false);
    void RemoveListener(ServoEventListener* observer);
    void AddListenerOnComplete(void(*onEvent)(uint16_t id, ServoId channel));
    void RemoveListenerOnComplete(void(*onEvent)(uint16_t id, ServoId channel));
    void AddListenerOnAbort(void(*onEvent)(uint16_t id, ServoId channel));
    void RemoveListenerOnAbort(void(*onEvent)(uint16_t id, ServoId channel));
    void AddListenerOnError(void(*onEvent)(uint16_t id, ServoId channel));
    void RemoveListenerOnError(void(*onEvent)(uint16_t id, ServoId channel));
    void ServoComplete(uint16_t id, ServoId channel);
    void ServoAbort(uint16_t id, ServoId channel);
    void ServoError(uint16_t id, ServoId channel);
}
