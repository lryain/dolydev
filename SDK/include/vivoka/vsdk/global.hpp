/// @file      global.hpp
/// @author    Pierre Caissial
/// @date      Created on 25/02/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <string>

#define VSDK_VERSION_CHECK(major, minor, patch) \
    ((major & 0xFF) << 16 | (minor & 0xFF) << 8 | (patch & 0xFF))

#define VSDK_VERSION VSDK_VERSION_CHECK(VSDK_VERSION_MAJOR, VSDK_VERSION_MINOR, VSDK_VERSION_PATCH)

namespace Vsdk
{
    /// Returns a @c "major.minor.patch" formatted version string
    std::string version();
} // !namespace Vsdk
