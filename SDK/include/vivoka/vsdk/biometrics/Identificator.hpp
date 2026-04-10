/// @file      Identificator.hpp
/// @author    Pierre Caissial
/// @date      Created on 01/07/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "Recognizer.hpp"

namespace Vsdk { namespace Biometrics
{
    enum class IdentificatorEventCode  {};
    enum class IdentificatorErrorCode  { UnexpectedError };

    /// ConsumerModule that performs user identification (finding who's talking from a bunch of
    /// registered users)
    class Identificator
        : public Recognizer<IdentificatorEventCode, IdentificatorErrorCode>
    {
    public:
        using Recognizer::Recognizer;
    };

    using IdentificatorPtr = std::shared_ptr<Identificator>;
}} // !namespace Vsdk::Biometrics
