/// @file      File.hpp
/// @author    Pierre Caissial
/// @date      Created on 19/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../Pipeline.hpp"

// C++ includes
#include <cstdio>
#include <memory>

namespace Vsdk { namespace Audio { namespace Consumer
{
    /// Writes incoming audio buffers into a file
    class File : public ConsumerModule
    {
    private:
        std::unique_ptr<FILE, void(*)(FILE *)> _file;

    public:
        explicit File(std::string const & path, bool truncate);

    private:
        void process(Buffer const & buffer, bool last) override;
    };
}}} // !Vsdk::Audio::Consumer
