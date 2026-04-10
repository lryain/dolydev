/// @file      Singleton.hpp
/// @copyright Copyright (c) Vivoka (vivoka.com)
/// @author    Pierre Caissial
/// @date      Created on 29/06/2020

#pragma once

// VSDK includes
#include <vsdk/Exception.hpp>

// C++ includes
#include <memory>
#include <typeinfo>

namespace Vsdk { namespace Utils
{
    /// Generic CRTP singleton abstraction
    /// @note   To correctly disable publicly constructing instances, <tt>T</tt>'s constructors and
    ///         destroy should not be public. You then need to friend this class and the
    ///         @c std::default_delete<T> class to prevent compilation errors.
    template<typename T>
    class Singleton
    {
    private:
        static std::unique_ptr<T> _instance;

    protected:
        Singleton() = default;

    public:
        /// @throw  Exception if called in initialized state to prevent silent destruction of
        ///         previous instance.
        template<typename... Args>
        static T & init(Args &&... args)
        {
            VSDK_B_ASSERT(_instance == nullptr, "Singleton<{}> should be destroyed before "
                                                "getting initialized again", typeid(T).name());
            _instance.reset(new T(std::forward<Args>(args)...));
            return *_instance;
        }

        /// @return  @c true if already initialized, else @c false.
        static bool hasInstance()
        {
            return !!_instance;
        }

        /// @throw  Exception when called in uninitialized state.
        static T & instance()
        {
            VSDK_B_ASSERT(_instance != nullptr,
                          "Singleton<{}> hasn't been initialized before access", typeid(T).name());
            return *_instance;
        }

        /// @note   Can be called even if not initialized.
        static void destroy()
        {
            _instance.reset();
        }
    };

    template<typename T> std::unique_ptr<T> Singleton<T>::_instance;
}} // !namespace Vsdk::Utils
