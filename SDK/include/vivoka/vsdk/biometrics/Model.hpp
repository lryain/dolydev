/// @file      Model.hpp
/// @author    Pierre Caissial
/// @date      Created on 25/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../audio/Buffer.hpp"
#include "../details/vsdk/StatusReporter.hpp"
#include "ModelType.hpp"

namespace Vsdk { namespace Biometrics
{
    class Engine;

    enum class ModelEventCode  { Info };
    enum class ModelErrorCode  { UnexpectedError };

    /// Used by Authenticator and Identificator instances, containing a bunch of audio records
    /// associated with user names
    class Model : public Vsdk::details::StatusReporter<ModelEventCode, ModelErrorCode>
    {
        friend class Vsdk::Biometrics::Engine;

    protected:
        Model(std::shared_ptr<Engine> engine, std::string name, ModelType type);

    public:
        virtual ~Model() noexcept;

    public:
        auto type() const -> ModelType;
        auto name() const -> std::string const &;

        /// Gets the list of users registered in this model
        auto users() const -> std::vector<std::string> const &;

        /// Gets the path of the model file
        auto path() const -> std::string const &;

        /// Analyses a user record from a file
        /// @returns a pair comprising a boolean value 'accepted' indicating whether the record
        ///          has the default requirements or not, and a JSON object containing detailed
        ///          information about the record
        auto analyseRecord(std::string const & path)
            -> std::pair<bool, nlohmann::json>;

        /// Analyses a user record from an audio buffer
        /// @returns a pair comprising a boolean value 'accepted' indicating whether the record
        ///          has the default requirements or not, and a JSON object containing detailed
        ///          information about the record
        auto analyseRecord(Vsdk::Audio::Buffer const & buffer)
            -> std::pair<bool, nlohmann::json>;

    public:
        /// Adds a user record from a file
        void addRecord(std::string const & user, std::string const & path);

        /// Adds a user record from an audio buffer
        void addRecord(std::string const & user, Audio::Buffer buffer);

        /// Erases an user and all its audio records
        void eraseUser(std::string const & name);

        /// Finalizes the model before being used
        void compile();

    protected:
        virtual auto usersImpl() const -> std::vector<std::string> const & = 0;
        virtual auto pathImpl() const -> std::string const &  = 0;
        virtual auto analyseRecordImpl(std::string const & path)
            -> std::pair<bool, nlohmann::json> = 0;
        virtual auto analyseRecordImpl(Vsdk::Audio::Buffer const & buffer)
            -> std::pair<bool, nlohmann::json> = 0;

        virtual void addRecordImpl(std::string const & user, std::string const & path) = 0;
        virtual void addRecordImpl(std::string const & user, Audio::Buffer buffer) = 0;
        virtual void eraseUserImpl(std::string const & name) = 0;

        virtual void compileImpl() = 0;

    protected:
        void setModelType(ModelType type);
        void throwIfDeleted() const;

    private:
        void setDeleted(bool deleted);

    private:
        struct Pimpl;
        Pimpl * _pimpl = nullptr;
    };

    using ModelPtr = std::shared_ptr<Model>;
}} // !namespace Vsdk::Biometrics
