"""
Auto-generated static DolyCommand enum from docs/命令.md

生成规则:
- 使用 `docs/命令.md` 中出现的命令名作为枚举成员名（如 `iHeyDoly`）。
- 对文档中显式标注了 `0xNN` 的命令使用该值；未标注的按顺序分配上一个值 +1。

如果需要重新生成，请运行脚本从 md 解析并覆盖此文件。
"""
from enum import IntEnum, unique
from typing import Optional


@unique
class DolyCommand(IntEnum):
    iHeyDoly = 0x00
    iHelloDoly = 0x02
    iInterrupt = 0x03
    iTalking = 0x04
    iShutup = 0x05
    iEStop = 0x06
    ActWhoami = 0x07
    ActWhatcanudo = 0x08
    ActSing = 0x09
    ActDance = 0x0A
    ActKiss = 0x0B
    ActTellStory = 0x0C
    ActTakePhoto = 0x0D
    ActTakeVideo = 0x0E
    ActPlayMusic = 0x0F
    ActWeather = 0x10
    ActPlayRadio = 0x11
    ActPlayNews = 0x12
    ActGoHome = 0x13
    ActForward = 0x14
    ActBackward = 0x15
    ActTurnLeft = 0x16
    ActTurnRight = 0x17
    ActStop = 0x18
    ActTime = 0x19
    ActDate = 0x1A
    ActTimerCD = 0x1B
    ActTimerCD10 = 0x1C
    ActTimerCU = 0x1D
    ActAlarmClock = 0x1E
    ActOk = 0x1F
    ActStart = 0x20
    ActCancel = 0x21
    ActPuase = 0x22
    ActResume = 0x23
    ActPrevious = 0x24
    ActNext = 0x25
    ActReset = 0x26
    ActTurnLightOn = 0x27
    ActTurnLightOff = 0x28
    ActMakeFace = 0x29
    ActMakeSmile = 0x2A
    ActMakeGhost = 0x2B
    EyeBlink = 0x2C
    EyeBlinkL = 0x2D
    EyeBlinkR = 0x2E
    EyelidSquint = 0x2F
    EyelidSquintL = 0x30
    EyelidSquintR = 0x31
    EyeLookup = 0x32
    EyeLookdown = 0x33
    EyeLookL = 0x34
    EyeLookR = 0x35
    EyeCircle = 0x36
    EyeDouji = 0x37
    EyeDizz = 0x38
    EyeStunned = 0x39
    EyeSuperise = 0x3A
    EyeOpen = 0x3B
    EyeClose = 0x3C
    EyeStyle = 0x3D
    EyeCat = 0x3E
    EyeMech = 0x3F
    EyeIris = 0x40
    EyePupil = 0x41
    EyePupilInc = 0x42
    EyePupilDec = 0x43
    ArmRise = 0x44
    ArmRiseL = 0x45
    ArmRiseR = 0x46
    ArmACWave = 0x47
    ArmACWave1 = 0x48
    ArmACWave2 = 0x49
    ArmACWaveL = 0x4A
    ArmACWaveR = 0x4B
    ArmRGB = 0x4C
    ArmRGBBrth = 0x4D
    ArmRGBBup = 0x4E
    ArmRGBBDn = 0x4F
    ArmWSLEDSty = 0x50
    ArmWSLEDSty1 = 0x51
    ArmWSLEDSty2 = 0x52
    ArmWSLEDSty3 = 0x53
    ArmWSLEDSty4 = 0x54
    ArmWSLEDSty5 = 0x55
    SysVolUp = 0x56
    SysVolDown = 0x57
    SysVolMute = 0x58
    SysVolNorm = 0x59
    SysVolWispr = 0x5A
    StaRestTime = 0x5B
    StaSleep = 0x5C
    StaExplore = 0x5D
    StaGame1 = 0x5E
    StaGame2 = 0x5F
    StaGame3 = 0x60
    StaGame4 = 0x61
    StaGame5 = 0x62
    StaGame6 = 0x63
    NextGame = 0x64


def from_byte(value: int) -> Optional[DolyCommand]:
    try:
        return DolyCommand(value)
    except Exception:
        return None


def name_for_byte(value: int) -> Optional[str]:
    cmd = from_byte(value)
    return cmd.name if cmd is not None else None


HEX_TO_NAME = {member.value: member.name for member in DolyCommand}
