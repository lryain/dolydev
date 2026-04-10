/// @file VersionNumber.hpp
/// @author Robin Rebischung
/// @date Created on 14/2/2022
/// @copyright Copyright (c) 2020 Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <string>
#include <vector>

namespace Vsdk { namespace Utils
{
    class VersionNumber
    {
    public:
        VersionNumber() noexcept = default;
        VersionNumber(int major, std::string const & suffix = "");
        VersionNumber(int major, int minor, std::string const & suffix = "");
        VersionNumber(int major, int minor, int micro, std::string const & suffix = "");
        VersionNumber(std::initializer_list<int> args, std::string const & suffix = "");
        explicit VersionNumber(std::vector<int> segments, std::string const & suffix = "");
        VersionNumber(std::string const & str);
        VersionNumber(const char * str);

    public:
        bool isNormalized() const;
        bool isNull() const;

        /// Returns true if the current version number is contained in the other
        /// version number, otherwise returns false.
        bool isPrefixOf(VersionNumber const & other) const;

        int  majorVersion() const;
        int  minorVersion() const;
        int  microVersion() const;
        auto suffix() const -> std::string;

        /// Returns an equivalent version number but with all trailing zeros removed.
        /// To check if two numbers are equivalent, use normalized() on both version
        /// numbers before performing the compare.
        auto normalized() const -> VersionNumber;

        int  segmentAt(int index) const;
        int  segmentCount() const;
        auto segments() const -> std::vector<int> const &;

        auto toString(bool withSuffix = false) const -> std::string;

    public:
        static auto commonPrefix(VersionNumber const & v1, VersionNumber const & v2)
            -> VersionNumber;

        /// Compares v1 with v2 and returns an integer less than, equal to, or
        /// greater than zero, depending on whether v1 is less than, equal to, or
        /// greater than v2, respectively.
        /// Comparisons are performed by comparing the segments of v1 and v2
        /// starting at index 0 and working towards the end of the longer list.
        static int  compare(VersionNumber const & v1, VersionNumber const & v2) noexcept;
        static auto fromString(std::string const & str) -> VersionNumber;

    public:
        friend bool operator==(VersionNumber const & lhs, VersionNumber const & rhs)
        { return VersionNumber::compare(lhs, rhs) == 0; }

        friend bool operator!=(VersionNumber const & lhs, VersionNumber const & rhs)
        { return VersionNumber::compare(lhs, rhs) != 0; }

        friend bool operator< (VersionNumber const & lhs, VersionNumber const & rhs)
        { return VersionNumber::compare(lhs, rhs) < 0; }

        friend bool operator<=(VersionNumber const & lhs, VersionNumber const & rhs)
        { return VersionNumber::compare(lhs, rhs) <= 0; }

        friend bool operator> (VersionNumber const & lhs, VersionNumber const & rhs)
        { return VersionNumber::compare(lhs, rhs) > 0; }

        friend bool operator>=(VersionNumber const & lhs, VersionNumber const & rhs)
        { return VersionNumber::compare(lhs, rhs) >= 0; }

    private:
        std::vector<int> _segments;
        std::string      _suffix;
    };
}} // !namespace Vsdk::Utils


