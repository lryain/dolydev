/// @file      BufferForwarder.hpp
/// @author    Pierre CAISSIAL
/// @date      Created on 13/03/2022
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// VSDK includes
#include "../Pipeline.hpp"

namespace Vsdk { namespace Audio { namespace Producer
{
    class BufferForwarder : public ProducerModuleImpl<BufferForwarder>
    {
    public:
        /// Constructs a @c BufferForwarder with an @c accelerationRate of @c 1.0
        BufferForwarder() noexcept;

    public:
        /// Dumps the @p buffer into the pipeline
        /// @warning Does nothing if pipeline has not been started prior.
        void forward(Vsdk::Audio::Buffer buffer, bool last);

    protected:
        void startImpl() override;
        void stopImpl()  override;
    };

    using BufferForwarderPtr = std::shared_ptr<BufferForwarder>;
}}} // !namespace Vsdk::Audio::Producer
