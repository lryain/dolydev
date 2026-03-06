#pragma once
#include <stdint.h>

enum GpioType :uint8_t
{
	GPIO_INPUT = 0,
	GPIO_OUTPUT = 1,
	GPIO_PWM = 2
};

enum GpioState :bool
{
	LOW = false,
	HIGH = true,
};

enum PinId :uint8_t
{
	// >= 50 GPIO_CHIP2
	// Port 1 (8-15)
	// Tentative mapping for cliff sensor enables (confirm with hardware spec)
	IRS_DRV = 65, ///< 悬崖 IR 使能
	// TOF enable (left) on PCA9535 IO1_6, mapped as PinId 64 per hardware spec
	TOF_ENL = 64, ///< TOF 使能
	// Port 1 (8-15)
	EXT_IO_0 = 63, ///< 扩展 IO 5
	EXT_IO_1 = 62, ///< 扩展 IO 4
	EXT_IO_2 = 61, ///< 扩展 IO 3
	EXT_IO_3 = 60, ///< 扩展 IO 2
	EXT_IO_4 = 59, ///< 扩展 IO 1
	EXT_IO_5 = 58, ///< 扩展 IO 0

    // Port 0 (0-7)
    IRS_FL = 50,      ///< 前左悬崖传感器
    IRS_FR = 51,      ///< 前右悬崖传感器
    IRS_BL = 52,      ///< 后左悬崖传感器
    IRS_BR = 53,      ///< 后右悬崖传感器
    TOUCH_R = 54,     ///< 右触摸传感器
    TOUCH_L = 55,     ///< 左触摸传感器
    // SRV_L_EN = 56,    ///< 左舵机使能
    // SRV_R_EN = 57,    ///< 右舵机使能
	Pin_Servo_Left_Enable = 56,
	Pin_Servo_Right_Enable = 57,

};

enum PwmId :uint8_t
{
	Pwm_Fan = 2,
	Pwm_Led_Left_B = 6,
	Pwm_Led_Left_G = 7,
	Pwm_Led_Left_R = 8,
	Pwm_Led_Right_B = 9,
	Pwm_Led_Right_G = 10,
	Pwm_Led_Right_R = 11,
};

namespace GPIO
{
	// Initialize IO pin
	// state is optional for GPIO
	// return 0 = success
	// return -1 = initialized failed
	// return -2 = wrong type
	int8_t init(PinId id, GpioType type, GpioState state = GpioState::LOW);

	// Initialize PWM pin
	// return 0 = success
	// return -1 = initialized failed
	int8_t init(PwmId id);

	// return 0 success
	// return -1 write failed
	// return -2 undefined id
	int8_t writePin(PinId id, GpioState state);

	// return 0 success
	// return -1 write failed
	// return -2 undefined id
	int8_t writePwm(PwmId id, uint16_t value);

};
