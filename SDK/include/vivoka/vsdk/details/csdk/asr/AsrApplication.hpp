/// @file      AsrApplication.hpp
/// @author    Pierre Caissial
/// @date      Created on 13/03/2020
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk-csdk-core/kernel/Resource.hpp>

// C++ includes
#include <string>

namespace Vsdk { namespace details { namespace Csdk
{
    class AsrManager;

    /// Gather contexts and their parameters used by recognizers to achieve recognition
    class AsrApplication : public IResource
    {
    private:
        std::string _name;

    public:
        /// @param name Name of the application, as found in configuration.
        AsrApplication(std::string name, AsrManager const & asrManager);

    public:
        auto name() const -> std::string const &;
    };
}}} // !namespace Vsdk::details::Csdk
