/// @file      Exception.hpp
/// @author    Pierre Caissial
/// @date      Created on 06/01/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Third-party includes
#include <fmt/core.h>

// C++ includes
#include <exception>
#include <functional>
#include <iostream>
#include <string_view>
#include <vector>

namespace Vsdk
{
    /// Extends @c std::runtime_error with string formatting and an error code
    class Exception : public std::exception
    {
    public:
        struct SourceLocation
        {
            int         line;
            std::string fileBaseName;
            std::string functionName;

            SourceLocation() noexcept;
            SourceLocation(int line, std::string fileBaseName, std::string functionName) noexcept;
            SourceLocation(int line, std::string_view fileBaseName,
                           std::string_view functionName) noexcept;
        };

    private:
        SourceLocation _source;
        std::string    _message;
        int            _code; ///< Zero means uninitialized

    public:
        Exception() noexcept;

        explicit Exception(int code) noexcept;
        Exception(SourceLocation src, int code) noexcept;

        template<typename... Args>
        explicit Exception(fmt::format_string<Args...> fmt, Args &&... args)
        {
            init(SourceLocation(), fmt::format(std::move(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        Exception(SourceLocation src, fmt::format_string<Args...> fmt, Args &&... args)
        {
            init(std::move(src), fmt::format(std::move(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        Exception(int code, fmt::format_string<Args...> fmt, Args &&...args)
        {
            init(SourceLocation(), code, fmt::format(std::move(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        Exception(SourceLocation src, int code, fmt::format_string<Args...> fmt, Args &&...args)
        {
            init(std::move(src), code, fmt::format(std::move(fmt), std::forward<Args>(args)...));
        }

    public:
        /// Gets the error code associated with this exception, if given
        /// @return 0 if uninitialized, else a non-zero value
        auto code() const -> int;

        /// Gets the source code line number that generated the exception, if given
        /// @return 0 if location is unknown, else a non-zero value
        auto line() const -> int;

        /// Gets the source code file base name that generated the exception, if given
        /// @return Empty string if location is unknown, else a non-zero value
        auto fileBaseName() const -> std::string_view;

        /// Gets the function name that generated the exception, if given
        /// @warning Different compilers will wield different naming convention, do not rely on this
        ///          string being the same, even between two runs of the same program!
        /// @returns Empty string if function name is unknown, else a non-zero value
        auto functionName() const -> std::string_view;

        auto what() const noexcept -> char const * override;

    private:
        void init(SourceLocation src, std::string message);
        void init(SourceLocation src, int code);
        void init(SourceLocation src, int code, std::string message);
    };

    bool operator==(Vsdk::Exception::SourceLocation const & lhs,
                    Vsdk::Exception::SourceLocation const & rhs);

    /// Packs a @c std::exception_ptr with a @c Vsdk::Exception::SourceLocation to locate its origin
    struct ExceptionBox
    {
        std::exception_ptr        ptr;
        Exception::SourceLocation sourceLocation;

        explicit operator bool() const { return ptr.operator bool(); }
    };

    using ExceptionStack = std::vector<Exception>;

    namespace details
    {
        auto fileName(std::string_view) -> std::string_view;
    } // !namespace details

    auto sandBox(Vsdk::Exception::SourceLocation, std::function<void()>) -> ExceptionBox;

    template<typename F, typename... Args>
    inline auto sandBox(Exception::SourceLocation src, F && f, Args &&... args) -> ExceptionBox
    {
        std::function<void()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        return sandBox(std::move(src), std::move(func));
    }

    auto getExceptionStack(Exception const & e) -> ExceptionStack;

    void printExceptionStack(std::exception const & e, int level = 0, int increment = 1,
                             std::string_view prefix = " * ", std::string_view suffix = "\n",
                             std::ostream & out = std::cerr);

    void printExceptionStack(Exception const & e, int level = 0, int increment = 1,
                             std::string_view prefix = " * ", std::string_view suffix = "\n",
                             std::ostream & out = std::cerr);

    void printExceptionStack(ExceptionStack const & e, int level = 0, int increment = 1,
                             std::string_view prefix = " * ", std::string_view suffix = "\n",
                             std::ostream & out = std::cerr);

    /// Shorter overload for when you like the formatting as it is but need another destination
    inline void printExceptionStack(std::exception const & e, std::ostream & out)
    {
        printExceptionStack(e, 0, 1, " * ", "\n", out);
    }

    /// Shorter overload for when you like the formatting as it is but need another destination
    inline void printExceptionStack(Vsdk::Exception const & e, std::ostream & out)
    {
        printExceptionStack(e, 0, 1, " * ", "\n", out);
    }

    /// Shorter overload for when you like the formatting as it is but need another destination
    inline void printExceptionStack(ExceptionStack const & s, std::ostream & out)
    {
        printExceptionStack(s, 0, 1, " * ", "\n", out);
    }
} // !namespace Vsdk

#ifndef VSDK_EXCEPTION_LINE
# define VSDK_EXCEPTION_LINE __LINE__
#endif

#ifndef VSDK_EXCEPTION_FILENAME
# define VSDK_EXCEPTION_FILENAME Vsdk::details::fileName(__FILE__)
#endif

#ifndef VSDK_EXCEPTION_FUNC
# define VSDK_EXCEPTION_FUNC __FUNCTION__
#endif

#define VSDK_SOURCE_LOCATION {VSDK_EXCEPTION_LINE, VSDK_EXCEPTION_FILENAME, VSDK_EXCEPTION_FUNC}

#define VSDK_EXCEPTION(...) Vsdk::Exception(VSDK_SOURCE_LOCATION, __VA_ARGS__)

/// Same as a regular throw of a @c Vsdk::Exception but adds source location info to the call
#define VSDK_THROW(...) throw VSDK_EXCEPTION(__VA_ARGS__)

/// Rethrows the current exception but nested under a new @c Vsdk::Exception formatted
/// with format and variadic arguments
#define VSDK_THROW_NESTED(...) std::throw_with_nested(VSDK_EXCEPTION(__VA_ARGS__))

/// Boolean assertion. Throws a @c Vsdk::Exception formatted with format and variadic arguments
/// if @p expr is @c false
#define VSDK_B_ASSERT(expr, ...) do { if (!(expr)) VSDK_THROW(__VA_ARGS__); } while (0)

/// Status code assertion. Throws a @c Vsdk::Exception initialized with @p code and formatted
/// with format and variadic arguments if @p code is not 0
#define VSDK_C_ASSERT(code, ...) VSDK_B_ASSERT((code) == 0, code, __VA_ARGS__)

/// Boolean assertion. Throws a @c Vsdk::Exception formatted with format and variadic arguments
/// if @p expr is @c false
#define VSDK_BC_ASSERT(expr, code, ...) VSDK_B_ASSERT(expr, code, __VA_ARGS__)

/// Status code assertion. Throws a @c Vsdk::Exception initialized with @p code and formatted
/// with format and variadic arguments if @p exitCode is not 0
#define VSDK_CC_ASSERT(exitCode, code, ...) VSDK_B_ASSERT((exitCode) == 0, code, __VA_ARGS__)

/// Catches any exception thrown by @p callable and rethrows it but nested under a new exception as
/// a formatted message with format and variadic arguments
#define VSDK_F_ASSERT(callable, ...) \
    do try { (callable)(); } catch (...) { VSDK_THROW_NESTED(__VA_ARGS__); } while (0)

/// Rethrows @p exceptionPtr if not null, nested under a new exception formatted with format and
/// variadic arguments
#define VSDK_E_ASSERT(exceptionPtr, ...) \
    VSDK_F_ASSERT([&] { if (exceptionPtr) std::rethrow_exception(exceptionPtr); }, __VA_ARGS__)

#define VSDK_SANDBOX(callable) Vsdk::sandBox(VSDK_SOURCE_LOCATION, callable)
