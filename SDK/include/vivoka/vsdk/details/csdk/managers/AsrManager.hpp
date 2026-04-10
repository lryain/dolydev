/// @file      AsrManager.hpp
/// @copyright Copyright (c) Vivoka (vivoka.com)
/// @author    Pierre Caissial
/// @date      Created on 13/03/2020

#pragma once

// Project includes
#include "../asr/AsrApplication.hpp"
#include "../asr/DynamicContentConsumer.hpp"

// VSDK includes
#include <vsdk/utils/Singleton.hpp>

// C++ includes
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Vsdk { namespace details { namespace Csdk
{
    class SystemManager;
    class Configuration;
    class Recognizer;

    /// Manage ASR resources: recognition applications, DCCs, ...
    class AsrManager : public IResource, public Utils::Singleton<AsrManager>
    {
        friend class Utils::Singleton<AsrManager>;

    private:
        std::string                                             _name;
        std::unordered_map<std::string, DynamicContentConsumer> _dccs;
        std::unordered_map<std::string, AsrApplication>         _applications;

    private:
        /// @param name Name of the manager, as found in configuration.
        /// @throw      Exception on initialization failure.
        AsrManager(std::string name, Configuration const & config,
                   SystemManager const & systemManager);

    public:
        /// @brief              Activate a list of search contexts on the specified @p recognizer at
        ///                     the specified @p startTime.
        /// @param startTime    Time at which recognition starts. Negative means right now.
        /// @param endTime      Time at which recognition stops if no result have been found.
        ///                     Negative means waiting infinitely.
        /// @throw              Exception if activation failed.
        void setApplications(std::unordered_set<std::string> const & names,
                             Recognizer const & recognizer, int startTime = 0, int endTime = -1);

    public:
        /// Initialize and store a new DCC named @p name as found configuration
        auto addDynamicContentConsumer(std::string name) -> DynamicContentConsumer &;
        void removeDynamicContentConsumer(std::string const & name);

    public:
        auto name() const -> std::string const &;
    };
}}} // !namespace Vsdk::details::Csdk
