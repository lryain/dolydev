/// @file      csdk_asr_global.hpp
/// @author    Pierre Caissial
/// @date      Created on 25/02/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// VSDK includes
#include <vsdk/global.hpp>

#define VSDK_CSDK_ASR_VERSION VSDK_VERSION_CHECK(VSDK_CSDK_ASR_VERSION_MAJOR, \
                                                 VSDK_CSDK_ASR_VERSION_MINOR, \
                                                 VSDK_CSDK_ASR_VERSION_PATCH)

namespace Vsdk::Csdk::Asr
{
    /// Returns a @c "major.minor.patch" formatted version string
    std::string version();
} // !namespace Vsdk::Csdk::Asr
