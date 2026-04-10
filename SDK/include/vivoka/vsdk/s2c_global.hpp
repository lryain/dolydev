/// @file      s2c_global.hpp
/// @author    Pierre Caissial
/// @date      Created on 05/09/23
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// VSDK includes
#include <vsdk/global.hpp>

#define VSDK_S2C_VERSION \
    VSDK_VERSION_CHECK(VSDK_S2C_VERSION_MAJOR, VSDK_S2C_VERSION_MINOR, VSDK_S2C_VERSION_PATCH)

namespace Vsdk::S2c
{
    /// Returns a @c "major.minor.patch" formatted version string
    std::string version();
} // !namespace Vsdk::S2c
