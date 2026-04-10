/// @file      DynamicModel.hpp
/// @author    Pierre Caissial
/// @date      Created on 05/07/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Third-party includes
#include <nlohmann/json_fwd.hpp>

namespace Vsdk { namespace Asr
{
    /// Model with dynamic data added at runtime
    class DynamicModel
    {
    protected:
        std::string _name;

    protected:
        std::unordered_map<std::string, std::vector<std::vector<std::string>>> _data;

    protected: explicit DynamicModel(std::string name) noexcept;
    public:    virtual ~DynamicModel() = default;

    public:
        /// Name of this particular instance
        auto name() const -> std::string const &;

    public:
        /// Adds data to slot @p slotName. Combining all data for a given slot is equivalent to
        /// writing the following rule in your grammar:
        /// @code <slotName>: "value_1" | "value_2" | ...; @endcode
        /// @note    Adding the same value twice will overwrite the previous value @b and
        ///          phonetic transcriptions. Empty values are ignored altogether.
        /// @warning Some engines do not support custom phonetic (either one or multiple), refer
        ///          to the particular engine documentation for more information.
        void addData(std::string const & slotName, std::string value,
                     std::vector<std::string> phoneticTranscriptions = {});

        /// @see addData(std::string const &, std::string, std::vector<std::string>)
        void addData(std::string const & slotName, char const * value,
                     std::vector<std::string> phoneticTranscriptions = {});

        /// Overload that takes any value that's convertible to @c std::string through
        /// @c std::to_string()
        template<typename T>
        void addData(std::string const & slotName, T && t,
                     std::vector<std::string> phoneticTranscriptions = {});

        /// Removes the value @p exactValue from slot @p slotName
        void removeData(std::string const & slotName, std::string const & exactValue);

        /// Removes every value that matches @p predicate from slot @p slotName
        void removeData(std::string const & slotName,
                        std::function<bool(std::string const &)> predicate);

        /// Removes all values for every slot
        void clearData();

    public:
        /// Mandatory step before applying the model on a recognizer
        virtual void compile() = 0;

    protected:
        virtual void configure(nlohmann::json const &) {}
    };

    using DynamicModelPtr = std::shared_ptr<DynamicModel>;

    template<typename T>
    inline void DynamicModel::addData(std::string const & slotName, T && t,
                                      std::vector<std::string> phoneticTranscriptions)
    {
        addData(slotName, std::to_string(std::forward<T>(t)), std::move(phoneticTranscriptions));
    }
}} // !namespace Vsdk::Asr
