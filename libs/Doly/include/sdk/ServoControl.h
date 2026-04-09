#pragma once
#include <stdint.h>
#include "ServoEvent.h"

namespace ServoControl
{
	int8_t init();
	int8_t setServo(uint16_t id, ServoId channel, float angle, uint8_t speed, bool invert);
	int8_t abort(ServoId channel);
	int8_t release(ServoId channel);
	int8_t dispose();
	float getVersion();
}
