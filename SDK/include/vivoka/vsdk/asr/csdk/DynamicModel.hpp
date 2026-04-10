/// @file      DynamicModel.hpp
/// @author    Pierre Caissial
/// @date      Created on 06/07/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// VSDK includes
#include <vsdk/asr/DynamicModel.hpp>

namespace Vsdk { namespace Asr { namespace Csdk
{
    class DynamicModel : public Asr::DynamicModel
    {
        friend class Engine;

    private:
        explicit DynamicModel(std::string name) noexcept;

    public:
        void compile() override;

    private:
        void configure(nlohmann::json const & json) override;
    };
}}} // !namespace Vsdk::Asr::Csdk
