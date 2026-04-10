/// @file LanguageData.hpp
/// @author Auto generated file
/// @date Created on 24/09/2021
/// @copyright Copyright (c) 2021 Vivoka (vivoka.com)

#pragma once

// Project includes
#include "Locale.hpp"

#include <vsdk-locale/vsdk-locale_export.hpp>

// C++ includes
#include <string>
#include <unordered_map>
#include <utility>

namespace Vsdk
{
    struct LanguageData;
    using LanguageDataKey = std::pair<Locale::Language, Locale::Country>;
    using Map             = std::unordered_map<LanguageDataKey, LanguageData>;

    struct VSDK_LOCALE_EXPORT LanguageData
    {
        std::string languageCode;
        std::string countryCode;
        std::string enLanguageName;
        std::string enCountryName;
        std::string nativeLanguageName;
        std::string nativeCountryName;
        std::string cerenceCode;
        std::string sensoryCode;

        static const Map & languageData();
    };
} // !namespace Vsdk

namespace std
{
    template<>
    struct VSDK_LOCALE_EXPORT hash<Vsdk::LanguageDataKey>
    {
        std::size_t operator()(Vsdk::LanguageDataKey const & key) const noexcept
        {
            return static_cast<std::size_t>(key.first) << 16
                 | static_cast<std::size_t>(key.second);
        }
    };
} // !namespace std
