/// @file      Recognizer.hpp
/// @author    Pierre Caissial
/// @date      Created on 26/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../audio/Pipeline.hpp"
#include "../details/vsdk/StatusReporter.hpp"

namespace Vsdk { namespace Biometrics
{
    class Model;

    namespace details
    {
        class RecognizerBase
        {
        public:
            explicit RecognizerBase(std::string name);
            virtual ~RecognizerBase() noexcept;

        public:
            auto name()      const -> std::string const &;
            auto model()     const -> std::shared_ptr<Model> const &;
            auto threshold() const -> int;

        public:
            void setModel(std::shared_ptr<Model> model);

            /// Sets a threshold of confidence that must be reached for the result to be accepted
            /// @p threshold    Generally between 0 and 10 (depends on the provider, 10 means the engine
            ///                 will be the strictest)
            void setThreshold(int threshold);

        protected:
            virtual void onNewModelSet() = 0;

        private:
            struct Pimpl;
            Pimpl * _pimpl = nullptr;
        };
    } // !namespace details

    /// Base class for biometrics recognition
    template<typename EventCode, typename ErrorCode>
    class Recognizer
        : public details::RecognizerBase
        , public Audio::ConsumerModule
        , public Vsdk::details::StatusReporter<EventCode, ErrorCode>
    {
        using details::RecognizerBase::RecognizerBase;
    };
}} // !namespace Vsdk::Biometrics
