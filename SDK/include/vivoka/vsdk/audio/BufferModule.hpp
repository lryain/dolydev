/// @file      BufferModule.hpp
/// @author    Pierre Caissial
/// @date      Created on 09/12/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "Pipeline.hpp"

// C++ includes
#include <atomic>
#include <thread>

namespace Vsdk { namespace Audio
{
    class BufferModule : public ProducerModuleImpl<BufferModule>, public ConsumerModule
    {
    private:
        Buffer           _buffer;
        size_t           _fireRate;
        std::thread      _worker;
        std::atomic_bool _running;

    public:
        /// Constructs a BufferModule with a fireRate of zero
        BufferModule() noexcept;
        ~BufferModule() override;

        /// Constructs a BufferModule with a fireRate of @p fireRate
        explicit BufferModule(size_t fireRate) noexcept;

    public:
        auto buffer()         -> Buffer       & { return _buffer;   }
        auto buffer()   const -> Buffer const & { return _buffer;   }
        auto fireRate() const -> size_t         { return _fireRate; }

    public:
        void setFireRate(size_t fireRate) { _fireRate = fireRate; }

    protected:
        void openImpl()  override;
        void runImpl()   override;
        void startImpl() override;
        void stopImpl()  override;
        void closeImpl() override;

    private:
        void exec(bool async);
        void process(Buffer const & buffer, bool last) override;
    };
}} // !namespace Vsdk::Audio
