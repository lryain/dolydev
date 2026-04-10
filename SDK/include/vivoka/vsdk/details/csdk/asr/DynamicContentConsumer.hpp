/// @file      DynamicContentConsumer.hpp
/// @author    Pierre Caissial
/// @date      Created on 02/07/2020
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk-csdk-core/kernel/BackgroundOperation.hpp>
#include <vsdk-csdk-core/kernel/Resource.hpp>
#include <vsdk-csdk-core/utils/ProxyPtr.hpp>

// C++ includes
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#undef ERROR // Thanks, Windows

namespace Vsdk { namespace details { namespace Csdk
{
    class AsrManager;

    enum class DynamicContentConsumerEventCode
    {
        DataPreparationStarted,  /**< Raised when the dcc data preparation is started. */
        DataPreparationFinished, /**< Raised when the dcc has finished creating its dynamic data.
                                      The message contains the current total number of entries,
                                      and number of added, removed, failed entries */
        CreationStarted,         /**< Raised when the dcc creation is started. */
        CreationFinished,        /**< Raised when the dcc creation has finished. */
        CreationFailed,          /**< Raised when the dcc creation failed. Look into the log file
                                      for the details of the failure. */
        WithNEntries,            /**< Raised with the DCC entries count after the dcc creation
                                      succeeded. */
        DataId,                  /**< Raised with the DCC data id after the dcc creation
                                      succeeded. */
        NEntriesProcessed,       /**< Raised every time  a configured amount (progress_increment)
                                      of entries has been processed. */
    };

    struct DynamicContentConsumerEvent
    {
        DynamicContentConsumerEventCode code = DynamicContentConsumerEventCode::CreationFinished;
        std::string                     codeString;
        std::string                     message;
    };

    enum class DynamicContentConsumerErrorCode
    {
#if CSDK_VERSION_MAJOR <= 2
        AllocationFailure,         ///< An error occurred during memory allocation
        ConfigInvalid,             ///< An error occurred while parsing the configuration file
        RemoteConnectionFailed,    ///< An error occurred while making a remote connection
        RemoteConnectionLost,      ///< A remote connection was lost
        RemoteDataUploadFailed,    ///< An error occurred while uploading data to the server
        DictionaryInvalid,         ///< A dictionary stream was of invalid type
        RulesetInvalid,            ///< A RULESET file is invalid
        ClcInvalid,                ///< A CLC file is invalid
        DictionaryNotFound,        ///< A dictionary has not been found
        RulesetNotFound,           ///< A RULESET file has not been found
        ClcNotFound,               ///< A CLC file has not been found
        AcmodInvalid,              ///< An ACMOD file is invalid
        AcmodNotFound,             ///< An acMod file is not found
        NoTranscription,           ///< No transcription was found for the specified word
        PersistedContextInvalid,   ///< A persisted context is invalid
        EwfNotFound,               ///< An EWF file has not been found
        PersistingContextFailed,   ///< An error occurred while persisting a dynamic context
        ContextCreationFailed,     ///< An error occurred while creating a context
        SpellTreeCreationFailed,   ///< An error occurred while creating a spell tree
        Error,                     ///< A generic error is occurred
        Fatal,                     ///< A fatal error is occurred
        PersistedSpellTreeInvalid, /**< The spell tree which was persisted by a spelling DCC
                                        is invalid */
        PersistedContextNotFound,  ///< Persisted context does not exist for the created DCC
        EntryNotAdded,             ///< An entry could not be added to a context in the DCC
        EntryWuwScoreBad,          /**< An entry is for wake up word but the quality of the word
                                        is bad */
        EntryWuwScoreWeak,         /**< An entry is for wake up word but the quality of
                                        the word is not quite good */
        EntryNotRemoved,           ///< An entry could not be removed from a context in the DCC
        EntryCountUnknown,         ///< The number of entries in the DCC is unknown
        CopInvalid,                ///< A cop file is invalid
#else
        UnexpectedFailure,         ///< An unexpected error occurred
        AllocationFailure,         ///< An error occurred during memory allocation
        ConfigInvalid,             ///< An error occurred while parsing the configuration file
        RemoteConnectionFailed,    ///< An error occurred while making a remote connection
        RemoteConnectionLost,      ///< A remote connection was lost
        RemoteDataUploadFailed,    ///< An error occurred while uploading data to the server
        DictionaryInvalid,         ///< A dictionary stream was of invalid type
        RulesetInvalid,            ///< A RULESET file is invalid
        ClcInvalid,                ///< A CLC file is invalid
        DictionaryNotFound,        ///< A dictionary has not been found
        RulesetNotFound,           ///< A RULESET file has not been found
        ClcNotFound,               ///< A CLC file has not been found
        AcmodInvalid,              ///< An ACMOD file is invalid
        AcmodNotFound,             ///< An acMod file is not found
        NoTranscription,           ///< No transcription was found for the specified word
        PersistedContextInvalid,   ///< A persisted context is invalid
        EwfNotFound,               ///< An EWF file has not been found
        PersistingContextFailed,   ///< An error occurred while persisting a dynamic context
        ContextCreationFailed,     ///< An error occurred while creating a context
        SpellTreeCreationFailed,   ///< An error occurred while creating a spell tree
        Error,                     ///< A generic error is occurred
        Fatal,                     ///< A fatal error is occurred
        PersistedSpellTreeInvalid, /**< The spell tree which was persisted by a spelling DCC
                                        is invalid */
        PersistedContextNotFound,  ///< Persisted context does not exist for the created DCC
        EntryNotAdded,             ///< An entry could not be added to a context in the DCC
        EntryWuwScoreBad,          /**< An entry is for wake up word but the quality of the word
                                        is bad */
        EntryWuwScoreWeak,         /**< An entry is for wake up word but the quality of
                                        the word is not quite good */
        EntryNotRemoved,           ///< An entry could not be removed from a context in the DCC
        EntryCountUnknown,         ///< The number of entries in the DCC is unknown
        CopInvalid,                ///< A cop file is invalid
#endif
    };

    enum class DynamicContentConsumerErrorType { Error, Warning };

    struct DynamicContentConsumerError
    {
        DynamicContentConsumerErrorType type;
        std::string                     typeString;
        DynamicContentConsumerErrorCode code;
        std::string                     codeString;
        std::string                     message;
    };

    class ContentValueIterator : public IResource
    {
    public:
        using ContentData = std::unordered_map<std::string, std::vector<std::vector<std::string>>>;

    private:
        std::size_t _idx = 0;
        std::string _lastError;
        std::unordered_map<std::string, std::vector<std::vector<std::string>>> _data;

    public:
        explicit ContentValueIterator(ContentData data);
        ContentValueIterator(ContentValueIterator &&) = delete;
        ContentValueIterator & operator=(ContentValueIterator &&) = delete;

    public:
        bool start();
        bool next();
        bool get(void * values);
        bool finish();

    public:
        auto lastError() const -> std::string const &;
    };

    /// Used by recognizers to create contexts at runtime
    class DynamicContentConsumer : public IResource, public BackgroundOperation
    {
        friend class AsrManager; // private ctor

    private:
        std::unique_ptr<Utils::ProxyPtr<DynamicContentConsumer>> _this;
        Utils::Pimpl<IResource>                                  _listener;
        std::string                                              _name;

    private:
        using ECallback = std::function<void(DynamicContentConsumerEvent const &)>;
        using WCallback = std::function<void(DynamicContentConsumerError const &)>;
        ECallback _eventCallback;
        WCallback _errorCallback;

    private: // Force user to create DCCs though the AsrManager
        DynamicContentConsumer(std::string name, AsrManager const & asrManager);

    public:
        DynamicContentConsumer(DynamicContentConsumer && dcc) noexcept;

    public:
        auto startDataPreparation(ContentValueIterator const & iterator)
            -> DynamicContentConsumer &;
        /// @brief      Asynchronously start the dynamic context creation from a COP file
        /// @note       Calling this while a data preparation is in progress will do nothing
        /// @warning    If you've already created previous data with the DCC, new data will
        ///             automatically be added to old ones, not replaced
        auto startDataPreparationFromCopFile(std::string const & path)
            -> DynamicContentConsumer &;

    public:
        auto name() const -> std::string const &;

    public:
        void subscribe(ECallback callback);
        void subscribe(WCallback callback);
        void cancelEventsSubscription();
        void cancelErrorsAndWarningsSubscription();

    public:
        /// @brief  Abort current operation as well as all queued operations. Does nothing if no
        ///         operations have been queued yet
        void abort() const override;
        /// @brief  Block until the background operation finishes, or during @p timeOut ms
        ///         or til an error occurs
        auto wait(std::chrono::milliseconds timeOut = 0ms) const
            -> BackgroundOperationStatus override;
        auto status() const -> BackgroundOperationStatus override;

    public:
        /// @brief  Internal function
        void onEvent(DynamicContentConsumerEvent const & event);
        /// @brief  Internal function
        void onError(DynamicContentConsumerError const & error);
    };
}}} // !namespace Vsdk::details::Csdk
