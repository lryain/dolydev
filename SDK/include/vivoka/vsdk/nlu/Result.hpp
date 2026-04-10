/// @file      Result.hpp
/// @author    Vincent Leroy
/// @date      Created on 10/1/2023
/// @copyright Copyright (c) 2023 Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <string>
#include <optional>
#include <vector>

namespace Vsdk { namespace Nlu
{
    struct Intent
    {
        std::optional<std::string> name;             ///< Name of the intent if it's been found
        float                      confidence = 0.f; ///< Intent's confidence score in range [0 ; 1]
    };

    struct Entity
    {
        std::string name;             ///< Name of the entity has specified during training
        std::string value;            ///< Value associated with this entity
        float       confidence = 0.f; ///< Confidence score for this entity between 0 and 1
        std::size_t startIndex = 0;   ///< Character index at which the entity starts in the sentence
        std::size_t endIndex   = 0;   ///< Character index at which the entity stops in the sentence
    };

    using EntityVector = std::vector<Entity>;

    struct Result
    {
        std::string  lang;             ///< Language of the model used to parse the sentence
        std::string  originalSentence; /**< The exact sentence that's been passed to
                                            @c Parser::process() **/
        Intent       intent;           ///< The recognized intent
        EntityVector entities;         ///< A vector of recognized entities (if any)
    };
}} // !namespace Vsdk::Nlu
