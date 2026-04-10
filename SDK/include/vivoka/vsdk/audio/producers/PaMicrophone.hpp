/// @file      PaMicrophone.hpp
/// @author    Pierre Caissial
/// @date      Created on 20/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk/Exception.hpp>
#include <vsdk/audio/Pipeline.hpp>

namespace Vsdk { namespace Audio { namespace Producer
{
    struct StreamCallback;

    class PaMicrophone : public Vsdk::Audio::ProducerModuleImpl<PaMicrophone>
    {
        friend struct StreamCallback;

    public:
        PaMicrophone();
        PaMicrophone(int channelCount, int sampleRate);
        PaMicrophone(int channelCount, int sampleRate, int framesPerBuffer);
        PaMicrophone(std::string deviceName, int channelCount, int sampleRate);
        PaMicrophone(std::string deviceName, int channelCount, int sampleRate, int framesPerBuffer);
        ~PaMicrophone() override;

        PaMicrophone(PaMicrophone const &) noexcept = delete;
        PaMicrophone(PaMicrophone &&) noexcept = delete;

        PaMicrophone & operator=(PaMicrophone const &) noexcept = delete;
        PaMicrophone & operator=(PaMicrophone &&) noexcept = delete;

    public:
        auto name()             const -> std::string const &;
        auto sampleRate()       const -> int;
        auto channelCount()     const -> int;

    private:
        void openImpl()   override;
        void startImpl()  override;
        void pauseImpl()  override;
        void resumeImpl() override;
        void stopImpl()   override;
        void closeImpl()  override;

    private:
        int onAudioReceived(int16_t const * samples, std::size_t frameCount);

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}}} // !namespace Vsdk::Audio::Producer::PortAudio
