/// @file      ConfigHelpers.hpp
/// @author    Pierre Caissial
/// @date      Created on 23/08/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../../utils/Misc.hpp"

#include <vsdk/Exception.hpp>

// Third-party includes
#include <fmt/ranges.h>

// C++ includes
#if __cplusplus >= 201703L && (!defined(__has_include) || __has_include(<string_view>))
# include <string_view>
#endif

#define ASSERT_TYPE(object, type, path)                                    \
    VSDK_B_ASSERT(object.is_ ## type(), "'{}' type must be '{}', is '{}'", \
                  path, #type, object.type_name())

#define ASSERT_TYPE_NUMBER(object, number_type, path) \
    VSDK_B_ASSERT(object.is_number_ ## number_type(), "'{}' expect {} value", path, #number_type)

#define ASSERT_HAS_WITH_TYPE(object, key, type, path) do {                           \
        VSDK_B_ASSERT(object.contains(key), "Missing '{}' (type: {})", path, #type); \
        ASSERT_TYPE(object[key], type, path);                                        \
    } while ((void)0, 0)

#define ASSERT_VALUE_EXPR(expr, path, value, msg) \
    VSDK_B_ASSERT(expr, "Wrong '{}' value '{}': {}", path, value, msg)

#define ASSERT_VALUE_EQ(object, key, value, path, msg) \
    ASSERT_VALUE_EXPR(object.at(key) == value, path, value, msg)

#define ASSERT_KEY_RANGE(value, range)                                                   \
    VSDK_B_ASSERT([&] { return ALGO(std::find, range, value) != std::cend(range); }(),   \
                  "Unknown key '{}', possible keys: [{}]", value, fmt::join(range, ", "))

#define ASSERT_VALUE_RANGE(value, range, path, msg)                                        \
    ASSERT_VALUE_EXPR([&] { return ALGO(std::find, range, value) != std::cend(range); }(), \
                      path, value, msg)

#define KEY_PATH(...) Vsdk::ConfigHelpers::keyPath(__VA_ARGS__)

namespace Vsdk { namespace ConfigHelpers
{
    template<typename... Args>
    inline auto keyPath(Args &&... args)
    {
        return fmt::format("{}",
#if __cplusplus >= 201703L && (!defined(__has_include) || __has_include(<string_view>))
            fmt::join(std::array<std::string_view,sizeof...(args)>{std::forward<Args>(args)...},".")
#else
            fmt::join(std::array<std::string, sizeof...(args)>{std::forward<Args>(args)...}, ".")
#endif
        );
    }
}} // !Vsdk::ConfigHelpers
