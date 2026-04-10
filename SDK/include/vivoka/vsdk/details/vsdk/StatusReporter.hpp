/// @file      StatusReporter.hpp
/// @author    Pierre Caissial
/// @date      Created on 24/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Third-party includes
#include <nlohmann/json.hpp>

// C++ includes
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#ifdef ERROR
# undef ERROR
#endif

namespace Vsdk { namespace details
{
    struct ResultHypothesis
    {
        using Tag = std::pair<std::string, std::string>;

        int              confidence = -1;
        int              beginTime, endTime;
        std::string      text, startRule;
        std::vector<Tag> tags;
    };

    void extractHypotheses(nlohmann::json const & items, std::vector<ResultHypothesis> & hyp);

    enum class StatusResultType { Asr, Nlu, Identification, Authentication, Analysis };

    struct StatusResult
    {
        StatusResultType              type{};
        std::string                   typeString;
        nlohmann::json                json;
        std::vector<ResultHypothesis> hypotheses;
        bool                          isFinal{};
    };

    template<typename StatusEventCode,
             typename = std::enable_if<std::is_enum<StatusEventCode>::value>>
    struct StatusEvent
    {
        StatusEventCode           code{};
        std::string               codeString;
        std::string               message;
        std::chrono::milliseconds timeMarker{};
    };

    enum class StatusErrorType { Error, Warning };

    template<typename StatusErrorCode,
             typename = std::enable_if<std::is_enum<StatusErrorCode>::value>>
    struct StatusError
    {
        StatusErrorType type{};
        StatusErrorCode code{};
        std::string     codeString;
        std::string     message;
    };

    template<typename EvtCode, typename ErrCode>
    class StatusReporter
    {
    public:
        using Result         = StatusResult;
        using ResultType     = StatusResultType;
        using Event          = StatusEvent<EvtCode>;
        using EventCode      = EvtCode;
        using Error          = StatusError<ErrCode>;
        using ErrorType      = StatusErrorType;
        using ErrorCode      = ErrCode;
        using ResultCallback = std::function<void(Result const &)>;
        using EventCallback  = std::function<void(Event  const &)>;
        using ErrorCallback  = std::function<void(Error  const &)>;

    private:
        ResultCallback _resultCallback;
        EventCallback  _eventCallback;
        ErrorCallback  _errorCallback;

    public:
        /// Install a callback invoked when receiving recognition results
        /// @note       Only one callback can be set at a time. Calling this again will replace
        ///             the previous callback
        /// @warning    @p callback might get invoked on another thread!
        void subscribe(ResultCallback callback)    { _resultCallback = std::move(callback); }
        /// Install a callback invoked when receiving recognition events
        /// @note       Only one callback can be set at a time. Calling this again will replace
        ///             the previous callback
        /// @warning    @p callback might get invoked on another thread!
        void subscribe(EventCallback callback)     { _eventCallback  = std::move(callback); }
        /// Install a callback invoked when receiving recognition errors/warnings
        /// @note       Only one callback can be set at a time. Calling this again will replace
        ///             the previous callback
        /// @warning    @p callback might get invoked on another thread!
        void subscribe(ErrorCallback callback)     { _errorCallback  = std::move(callback); }
        void cancelResultsSubscription()           { _resultCallback = ResultCallback();    }
        void cancelEventsSubscription()            { _eventCallback  = EventCallback();     }
        void cancelErrorsAndWarningsSubscription() { _errorCallback  = ErrorCallback();     }

    protected:
        void dispatchResult(ResultType resultType, std::string typeString,
                            nlohmann::json result, bool isFinal)
        {
            Result r;
            r.type       = resultType;
            r.typeString = typeString;
            r.json       = std::move(result);
            r.isFinal    = isFinal;

            if (r.type == ResultType::Asr)
                extractHypotheses(r.json, r.hypotheses);

            dispatchResult(std::move(r));
        }
        void dispatchResult(Result const & result)
        {
            if (_resultCallback)
                _resultCallback(result);
        }
        void dispatchEvent(EventCode eventCode, std::string codeString,
                           std::string message, std::chrono::milliseconds timeMarker)
        {
            dispatchEvent({ eventCode, std::move(codeString), std::move(message), timeMarker });
        }
        void dispatchEvent(Event const & event)
        {
            if (_eventCallback)
                _eventCallback(event);
        }
        void dispatchError(ErrorType errorType, ErrorCode errorCode,
                           std::string codeString, std::string message)
        {
            dispatchError({ errorType, errorCode, std::move(codeString), std::move(message) });
        }
        void dispatchError(Error const & error)
        {
            if (_errorCallback)
                _errorCallback(error);
        }
        void extractHypotheses(nlohmann::json const & items, std::vector<ResultHypothesis> & hyp) {
            details::extractHypotheses(items, hyp);
        }
    };
}} // !namespace Vsdk::details

#define TEMPLT_OP(op, type) \
    template<typename E> \
    inline bool op(Vsdk::details::type<E> const & lhs, Vsdk::details::type<E> const & rhs)

#define NORMAL_OP(op, type) \
    inline bool op(Vsdk::details::type const & lhs, Vsdk::details::type const & rhs)

NORMAL_OP(operator<,  StatusResult) { return lhs.typeString <  rhs.typeString; }
NORMAL_OP(operator>,  StatusResult) { return lhs.typeString >  rhs.typeString; }
NORMAL_OP(operator<=, StatusResult) { return lhs.typeString <= rhs.typeString; }
NORMAL_OP(operator>=, StatusResult) { return lhs.typeString >= rhs.typeString; }
NORMAL_OP(operator==, StatusResult) { return lhs.typeString == rhs.typeString; }
NORMAL_OP(operator!=, StatusResult) { return lhs.typeString != rhs.typeString; }

TEMPLT_OP(operator<,  StatusEvent)  { return lhs.codeString <  rhs.codeString; }
TEMPLT_OP(operator>,  StatusEvent)  { return lhs.codeString >  rhs.codeString; }
TEMPLT_OP(operator<=, StatusEvent)  { return lhs.codeString <= rhs.codeString; }
TEMPLT_OP(operator>=, StatusEvent)  { return lhs.codeString >= rhs.codeString; }
TEMPLT_OP(operator==, StatusEvent)  { return lhs.codeString == rhs.codeString; }
TEMPLT_OP(operator!=, StatusEvent)  { return lhs.codeString != rhs.codeString; }

TEMPLT_OP(operator<,  StatusError)  { return lhs.codeString <  rhs.codeString; }
TEMPLT_OP(operator>,  StatusError)  { return lhs.codeString >  rhs.codeString; }
TEMPLT_OP(operator<=, StatusError)  { return lhs.codeString <= rhs.codeString; }
TEMPLT_OP(operator>=, StatusError)  { return lhs.codeString >= rhs.codeString; }
TEMPLT_OP(operator==, StatusError)  { return lhs.codeString == rhs.codeString; }
TEMPLT_OP(operator!=, StatusError)  { return lhs.codeString != rhs.codeString; }

#undef TEMPLT_OP
