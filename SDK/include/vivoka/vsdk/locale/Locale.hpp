/// @file Locale.hpp
/// @author Robin Rebischung
/// @date Created on 11/9/2020
/// @copyright Copyright (c) 2020 Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk-locale/vsdk-locale_export.hpp>

// C++ includes
#include <string>
#include <vector>

namespace Vsdk
{
    /// Manages various representations of a language
    class VSDK_LOCALE_EXPORT Locale
    {
    public:
        enum class Language // ISO 639-3
        {
            Unknown,

            Arabic,
            Basque,
            Bengali,
            Bhojpuri,
            Bulgarian,
            Cantonese,
            Catalan,
            Chinese,
            Crotian,
            Czech,
            Danish,
            Dutch,
            English,
            Farsi,
            Finnish,
            French,
            Galician,
            German,
            Greek,
            Hebrew,
            Hindi,
            Hungarian,
            Indonesian,
            Italian,
            Japanese,
            Kannada,
            Korean,
            Latvian,
            Malay,
            Mandarin,
            Marathi,
            Norwegian,
            Polish,
            Portuguese,
            Romanian,
            Russian,
            Slovak,
            Slovenian,
            Spanish,
            Swedish,
            Tamil,
            Telugu,
            Thai,
            Turkish,
            Ukrainian,
            Valencian,
            Vietnamese,
        };

        enum class Country // ISO 3166-2
        {
            Unknown,

            Argentina,
            Australia,
            Belgium,
            Brazil,
            Bulgaria,
            Canada,
            Chile,
            China,
            ChinaShaanxi,
            ChinaShanghei,
            ChinaSichuan,
            Colombia,
            Croatia,
            Czechia,
            Denmark,
            Finland,
            France,
            Germany,
            Greece,
            HongKong,
            Hungary,
            India,
            IndiaJharkhand,
            IndiaKarnakata,
            IndiaTamilNadu,
            Indonesy,
            Ireland,
            Israel,
            Italy,
            Japan,
            Latvia,
            Malaysia,
            Mexico,
            Morocco,
            Netherlands,
            Norwegia,
            Poland,
            Portugal,
            Romania,
            Russia,
            Saudi,
            Scotland,
            Slovakia,
            Slovienia,
            SouthAfrica,
            SouthKorea,
            Spain,
            SpainGalicia,
            SpainValencia,
            Sweden,
            Taiwan,
            Thailand,
            Turkey,
            Ukrainia,
            UnitedKingdom,
            UnitedStates,
            Vietnam,
            World,

            // Vivoka extension
            NorthEastChina,
            PersianGulf,
        };

    public:
        /// Constructs a Locale object with unknown language and country.
        Locale() noexcept;

        /// Constructs a Locale object from the specified Language and country.
        /// @param language The name of the locale. It has the format "language-country",
        ///                 where language is a lowercase, two-letter ISO 639 language code,
        ///                 and  country is an uppercase, two- or three-letter ISO 3166
        ///                 country code.
        Locale(Language language, Country country) noexcept;

        /// Constructs a Locale object from the specified name.
        /// @param name The name of locale. It has the format "language-country", where
        ///             language is a lowercase, two-letter ISO 639 language code, and
        ///             country is an uppercase, two- or three-letter ISO 3166 country
        ///             code. If the name is invalid unknown values will be stored for
        ///             both language and country.
        Locale(std::string name);

    public:
        /// Returns true when the locale contains valid values.
        bool isValid() const;

        /// Returns the language of this locale.
        Language language() const;

        /// Returns the country of this locale.
        Country country() const;

        /// Returns the language and country of this locale as a string of the form
        /// "language-country", where language is a lowercase, two-letter ISO 639 language
        /// code, and country is an uppercase, two- or three-letter ISO 3166 country code.
        std::string name() const;

        /// Returns lowercase two-letter ISO 639 language code for the locale.
        std::string languageCode() const;

        /// Returns uppercase two- or three-letter ISO 3166 country code.
        std::string countryCode() const;

        /// Returns the english name of the language for the locale. For example "Spanish"
        /// for Spanish/Mexico locale.
        std::string enLanguageName() const;

        /// Returns the english name of the country for the locale. For example "Mexico"
        /// for Spanish/Mexico locale.
        std::string enCountryName() const;

        /// Returns a native name of the language for the locale. For example "Español de México"
        /// for Spanish/Mexico locale.
        std::string nativeLanguageName() const;

        /// Returns a native name of the country for the locale. For example "México"
        /// for Spanish/Mexico locale.
        std::string nativeCountryName() const;

        /// Returns the Cerence language code for this locale. For example "frf";
        std::string cerenceCode() const;

        /// Returns the Sensory language code for this locale. For example "frFR";
        std::string sensoryCode() const;

    public:
        bool operator==(Locale const & other) const;
        bool operator!=(Locale const & other) const { return !(*this == other); }

    public:
        /// Create Locale from Cerence language code
        /// @param code Cerence language code
        /// @return     Locale object
        static Locale fromCerenceCode(std::string const & code);

        /// Create Locale from Sensory language code
        /// @param code Sensory language code
        /// @return     Locale object
        static Locale fromSensoryCode(std::string const & code);

        /// Returns a Vector of all supported locale.
        /// @return Vector of Locale
        static std::vector<Locale> const & allLocales();

    private:
        Language _language;
        Country  _country;
    };
} // !namespace Vsdk

namespace std
{
    template<>
    struct VSDK_LOCALE_EXPORT hash<Vsdk::Locale>
    {
        std::size_t operator()(Vsdk::Locale const & locale) const
        {
            return static_cast<std::size_t>(locale.language()) << 16
                 | static_cast<std::size_t>(locale.country());
        }
    };
} // !namespace std
