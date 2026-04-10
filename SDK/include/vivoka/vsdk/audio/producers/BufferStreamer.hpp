/// @file      BufferStreamer.hpp
/// @author    Pierre CAISSIAL
/// @date      Created on 13/03/2022
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// VSDK includes
#include "../Pipeline.hpp"

namespace Vsdk { namespace Audio { namespace Producer
{
    class BufferStreamer : public ProducerModuleImpl<BufferStreamer>
    {
    private:
        Vsdk::Audio::Buffer _remainder;
        double              _accelerationRate;

    public:
        /// Constructs a @c BufferStreamer with an @c accelerationRate of @c 1.0
        BufferStreamer() noexcept;

    public:
        /// Synchronously streams the @p buffer into the pipeline at a rate of
        /// `sample rate × channel count` samples per second
        /// @param buffer If zero or >= @p buffer.size(), is equivalent to a call to forward(). If
        ///               its value is >= `sample rate × channel count`, more than a second will be
        ///               spent sleeping before the next audio dispatch is done
        /// @warning Does nothing if pipeline has not been started prior.
        void stream(Vsdk::Audio::Buffer buffer, bool last, std::size_t bufferSize);

    public:
        /// Sets acceleration rate. Cannot be less than @c 1.0
        void setAccelerationRate(double rate);

    protected:
        void startImpl() override;
        void stopImpl()  override;
    };

    using BufferStreamerPtr = std::shared_ptr<BufferStreamer>;
}}} // !namespace Vsdk::Audio::Producer
