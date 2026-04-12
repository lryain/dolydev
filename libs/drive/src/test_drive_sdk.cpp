#include <cstdint>
#include <iostream>

#include "sdk/DriveControl.h"
#include "sdk/DriveEvent.h"

static void onDriveComplete(std::uint16_t id) {
    std::cout << "Drive complete id=" << id << std::endl;
}

static void onDriveError(std::uint16_t id, DriveMotorSide side, DriveErrorType type) {
    std::cout << "Drive error id=" << id << " side=" << static_cast<int>(side)
              << " type=" << static_cast<int>(type) << std::endl;
}

static void onDriveStateChange(DriveType driveType, DriveState state) {
    std::cout << "Drive state type=" << static_cast<int>(driveType)
              << " state=" << static_cast<int>(state) << std::endl;
}

int main() {
    std::cout << "DriveControl version=" << DriveControl::getVersion() << std::endl;
    std::cout << "DriveControl active=" << (DriveControl::isActive() ? "true" : "false")
              << std::endl;

    DriveEvent::AddListenerOnComplete(onDriveComplete);
    DriveEvent::AddListenerOnError(onDriveError);
    DriveEvent::AddListenerOnStateChange(onDriveStateChange);

    DriveEvent::RemoveListenerOnComplete(onDriveComplete);
    DriveEvent::RemoveListenerOnError(onDriveError);
    DriveEvent::RemoveListenerOnStateChange(onDriveStateChange);

    return 0;
}
