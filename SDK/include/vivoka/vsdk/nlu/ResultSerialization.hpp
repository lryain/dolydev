/// @file      ResultSerialization.hpp
/// @author    Vincent Leroy
/// @date      Created on 10/1/2023
/// @copyright Copyright (c) 2023 Vivoka (vivoka.com)

#pragma once

// Project includes
#include "Result.hpp"

// Third-party includes
#include <nlohmann/json.hpp>

namespace Vsdk { namespace Nlu
{
    /// Serializes an @p Intent to a JSON format
    void to_json(nlohmann::json & j, Intent const & intent);

    /// Serializes an @p Entity to a JSON format
    void to_json(nlohmann::json & j, Entity const & entity);

    /// Serializes a @p Result to a JSON format
    void to_json(nlohmann::json & j, Result const & result);

    /// Deserializes an @p Intent from a JSON format
    void from_json(nlohmann::json const & j, Intent & intent);

    /// Deserializes an @p Entity from a JSON format
    void from_json(nlohmann::json const & j, Entity & entity);

    /// Deserializes a @p Result from a JSON format
    void from_json(nlohmann::json const & j, Result & result);
}} // !namespace Vsdk::Nlu
