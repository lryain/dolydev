/// @file      File.hpp
/// @author    Pierre Caissial
/// @date      Created on 18/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../Pipeline.hpp"

// C++ includes
#include <cstdio>
#include <memory>

namespace Vsdk { namespace Audio { namespace Producer
{
    /// Reads a 16-bit Little-Endian PCM audio file and sends its bytes up a Pipeline
    class File : public ProducerModuleImpl<File>
    {
        using Handle = std::unique_ptr<FILE, void(*)(FILE *)>;

    private:
        Handle        _file;
        std::string   _path;
        int           _sampleRate;
        int           _channelCount;
        std::size_t   _bufferSize;
        bool          _realTimeStreamingEnabled;
        double        _accelerationRate;

    public:
        /// Constructs a default audio file reader. Sets:
        /// <ul>
        ///   <li>sample rate to 16kHz,</li>
        ///   <li>channel count to 1</li>
        ///   <li>buffer size to 2048</li>
        ///   <li>real time streaming is disabled</li>
        /// </ul>
        explicit File(std::string path);

        /// @param bufferSize 0 means reading the whole file in one go
        explicit File(std::string path, int sampleRate, int channelCount, std::size_t bufferSize);

    public:
        /// @throws Vsdk::Exception if producer is opened or if @p path is not a valid file path
        void setFilePath(std::string path);

        /// @throws Vsdk::Exception if producer is opened or if @p sampleRate value is invalid
        void setSampleRate(int sampleRate);

        /// @throws Vsdk::Exception if producer is open ans has not been closed yet or if
        ///                         @p channelCount value is invalid
        void setChannelCount(int channelCount);

        /// @param bufferSize       Zero means reading the whole file in one buffer (beware)
        /// @throws Vsdk::Exception if producer is opened or if @p channelCount value is invalid
        void setBufferSize(std::size_t bufferSize);

        /// Enables real time streaming of the file at a rate of
        /// <tt>sample_rate × channel_count</tt> bytes per second
        /// @warning Streaming is made synchronously during the call to run()!
        void setRealTimeStreamingEnabled(bool enabled);

        /// Sets acceleration rate. Cannot be less than @c 1.0
        void setAccelerationRate(double rate);

    protected:
        void openImpl()  override;
        void runImpl()   override;
        void closeImpl() override;

    private:
        void setBufferSizeToFileSize();
    };

    using FilePtr = std::shared_ptr<File>;
}}} // !namespace Vsdk::Audio::Producer
