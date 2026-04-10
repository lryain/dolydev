/// @file      Parser.hpp
/// @author    Vincent Leroy
/// @date      Created on 25/1/2023
/// @copyright Copyright (c) 2023 Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <memory>
#include <string>

// Third-party includes
#include <nlohmann/json_fwd.hpp>

// Project includes
#include "../details/vsdk/StatusReporter.hpp"

namespace Vsdk { namespace Nlu
{
    enum class ParserEventCode
    {
    };

    enum class ParserErrorCode
    {
        UnexpectedError,       ///< Something unexpected happened, check the message for more info
        AlreadyInProgress,     ///< An operation is already in progress
    };

    /// Process text into intent and entities using Natural Language Understanding techniques
    class Parser : public details::StatusReporter<ParserEventCode, ParserErrorCode>
    {
    protected:
        std::string _name;

    protected: explicit Parser(std::string name) noexcept;
    public: virtual ~Parser() = default;

    public:
        /// Name of this particular Parser instance
        auto name() const -> std::string const &;

    public:
        /// Parse @p text into an intent / entities result
        /// @param text The text you want to process
        ///
        /// @note The result will be gicen through the result callback
        ///       installed (using the subscribe function) on this particular parser
        virtual void process(std::string const & text) = 0;

    protected:
        virtual void configure(nlohmann::json const &) {}
        void dispatchEvent(EventCode code, std::string message, std::chrono::milliseconds time);
        void dispatchError(ErrorType type, ErrorCode code, std::string message = std::string());

    private:
        auto codeToString(ParserEventCode code) -> char const *;
        auto codeToString(ParserErrorCode code) -> char const *;
    };

    using ParserPtr = std::shared_ptr<Parser>;
}} // !namespace Vsdk::Nlu
