/// @file      Buffer.hpp
/// @author    Pierre Caissial
/// @date      Created on 17/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <cstdint>
#include <string>
#include <vector>

namespace Vsdk { namespace Audio
{
    /// 16-bit PCM audio buffer used for all audio operations
    class Buffer
    {
    private:
        std::vector<int16_t> _data;
        std::size_t          _maxSize;
        int                  _sampleRate;
        int                  _channelCount;

    public:
        /// Default constructs an empty, 16kHz mono channel buffer
        Buffer() noexcept;
        /// Constructs an empty buffer with specified sample rate and channel count
        Buffer(int sampleRate, int channelCount);
        /// @warning Don't pass @c nullptr into @p data! Use Buffer(int, int) instead
        Buffer(int16_t const * data, std::size_t sz, int sampleRate, int channelCount);
        Buffer(std::vector<int16_t> data, int sampleRate, int channelCount);
        /// Converts a floating-point audio buffer into a 16-bit audio buffer
        /// @warning This is - by nature - a lossy operation!
        Buffer(std::vector<float> const & data, int sampleRate, int channelCount);

    public:
        void append(std::vector<int16_t> const & data);
        void append(int16_t const * data, std::size_t sz);

    public:
        auto sampleRate()   const -> int;
        auto channelCount() const -> int;
        auto maxSize()      const -> std::size_t;

        auto size() const    -> std::size_t;
        auto data() const    -> std::vector<int16_t> const &;
        auto data()          -> std::vector<int16_t> &;
        auto rawData()       -> int16_t *;
        auto rawData() const -> int16_t const *;
        auto takeData()      -> std::vector<int16_t> &&;
        bool empty() const;
        void clear();

    public:
        /// Sets the maximum amount of @c int16_t values this buffer can hold
        /// @note   <ul>
        ///           <li>0 means unlimited (@c std::vector::max_size()) ;</li>
        ///           <li>Any other value turns this buffer into a circular buffer ;</li>
        ///           <li>This is not a strict limit, the buffer can go past it sometimes ;</li>
        ///           <li>Reducing a previously set size is Undefined Behavior.</li>
        ///         </ul>
        /// @throws Vsdk::Exception if @p sz @c > @c std::vector::max_size()
        void setMaxSize(std::size_t sz);
        void setSampleRate(int rate);
        void setChannelCount(int count);
        /// Saves the buffer to a headerless (PCM) 16-bit audio file
        void saveToFile(std::string const & path, bool truncate = true) const;
    };
}} // !namespace Vsdk::Audio
