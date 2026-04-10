/// @file      Misc.hpp
/// @author    Pierre Caissial
/// @date      Created on 18/01/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <algorithm>
#include <chrono>
#include <istream>

/// If you're like me and would like something a bit more concise and readable
#define ALGO(algo, container, ...) algo(std::begin(container), std::end(container), __VA_ARGS__)

namespace Vsdk { namespace Utils
{
    template<typename... Str>
    inline bool extract(std::istream & is, char sep, Str &... str)
    {
        auto const _ = { static_cast<bool>(std::getline(is, str, sep))... };
        return ALGO(std::all_of, _, [] (bool b) { return b == true; });
    }

    /// Formats a duration in milliseconds into a fixed-size string
    /// @return A @c std::string formatted into @c "HH:MM:SS.zzz"
    auto formatTimeMarker(std::chrono::milliseconds duration) -> std::string;
}} // !namespace Vsdk::Utils
