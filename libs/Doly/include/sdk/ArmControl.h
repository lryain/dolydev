#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include "ArmEvent.h"

struct ArmData
{
	ArmSide side;
	float angle;
};

namespace ArmControl
{
	int8_t init();
	int8_t dispose();
	bool isActive();
	void Abort(ArmSide side);
	uint16_t getMaxAngle();
	int8_t setAngle(uint16_t id, ArmSide side, uint8_t speed, uint16_t angle, bool with_brake = false);
	ArmState getState(ArmSide side);
	std::vector<ArmData> getCurrentAngle(ArmSide side);
	float getVersion();

	// 高级动作接口
	void moveMultiDuration(const std::map<ArmSide, float>& targets, int duration_ms);
	void servoSwingOf(ArmSide side, float target_angle, uint8_t approach_speed, float swing_amplitude, uint8_t swing_speed, int count = -1);
	void startSwing(ArmSide side, float min_angle, float max_angle, int duration_one_way, int count);
	void liftDumbbell(ArmSide side, float weight, int reps);
	void dumbbellDance(float weight, float duration_sec);
	void waveFlag(ArmSide side, float flag_weight, int wave_count);
	void beatDrum(ArmSide side, float stick_weight, int beat_count);
	void paddleRow(ArmSide side, float paddle_weight, int stroke_count);
	void dualPaddleRow(float paddle_weight, int stroke_count);
}
