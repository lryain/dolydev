#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace doly::eye_engine {

enum class LogLevel {
    kTrace = 0,
    kDebug,
    kInfo,
    kWarn,
    kError,
    kFatal
};

struct LogContext {
    std::string service{"eye_engine"};
    std::string component;
    std::string trace_id;
};

struct LogRecord {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    LogContext context;
};

class Logger {
public:
    using Sink = std::function<void(const LogRecord&)>;

    static void setSink(Sink sink);
    static void resetSink();
    static void emit(LogLevel level,
                     std::string_view message,
                     std::optional<LogContext> context = std::nullopt);

private:
    static Sink& sink();
    static Sink& defaultSink();
};

}  // namespace doly::eye_engine
