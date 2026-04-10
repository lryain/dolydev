/// @file      Authenticator.hpp
/// @author    Pierre Caissial
/// @date      Created on 01/07/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "Recognizer.hpp"

namespace Vsdk { namespace Biometrics
{
    enum class AuthenticatorEventCode  {};
    enum class AuthenticatorErrorCode  { UnexpectedError };

    /// ConsumerModule that performs user authentication (telling if the user talking is the one
    /// we expected to be talking)
    class Authenticator : public Recognizer<AuthenticatorEventCode, AuthenticatorErrorCode>
    {
    public:
        using Recognizer::Recognizer;

    public:
        auto userToRecognize() const -> std::string const &;

    public:
        virtual void setUserToRecognize(std::string user);

    protected:
        std::string _userToRecognize;
    };

    using AuthenticatorPtr = std::shared_ptr<Authenticator>;
}} // !namespace Vsdk::Biometrics
