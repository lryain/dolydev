/// @file      PortAudio.hpp
/// @author    Pierre Caissial
/// @date      Created on 11/05/2023
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <string>
#include <vector>

namespace Vsdk { namespace Utils { namespace PortAudio
{
    enum class DeviceType { Any, Input, Output, Both };

    std::vector<std::string> availableDeviceNames(DeviceType type = DeviceType::Any);
    void printAvailableDeviceNames(DeviceType type = DeviceType::Any);
}}} // !namespace Vsdk::Utils::PortAudio
