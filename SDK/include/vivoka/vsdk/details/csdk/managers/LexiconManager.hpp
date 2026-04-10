/// @file      LexiconManager.hpp
/// @author    BOURAOUI Al-Moez L.A
/// @date      Created on 16/01/2025
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk-csdk-core/kernel/Resource.hpp>
#include <vsdk-csdk-core/managers/SystemManager.hpp>

// C++ includes
#include <string>

namespace Vsdk::details::Csdk
{
    class Configuration;

    /// First manager to be instanciated and used by others to configure themselves
    class LexiconManager : public IResource
    {
    protected:
        LexiconManager(Configuration const & config, SystemManager const & systemManager);

    public:
        static auto init(Configuration const & config, SystemManager const & systemManager)
            -> LexiconManager &;
        static bool hasInstance();
        static auto instance() -> LexiconManager &;
        static void destroy();

    private:
        static std::unique_ptr<LexiconManager> _instance;
    };
} // !namespace Vsdk::details::Csdk
