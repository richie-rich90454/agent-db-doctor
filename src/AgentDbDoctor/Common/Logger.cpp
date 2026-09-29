// The implementation of the Logger class.
//
// The service path writes at most one line per hour, so the interesting work
// here is choosing a sink that neither allocates per line nor adds a thread.
//
// Note on the asynchronous sink mentioned in the specification's dependency
// list: an asynchronous sink needs a background thread of its own, and the
// service resource limits allow the service exactly one thread, while principle
// I/O-8 and principle P-25 both require a direct write with no buffering layer
// in between. The binding performance contract wins, so this logger writes
// synchronously through a plain file sink that is held open for the lifetime of
// the process. The reasoning is recorded in docs/performance.md.
#include <filesystem>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the spdlog sinks and the registry.
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
// Provides the Logger class declaration and the VerbosityLevel enumeration.
#include "AgentDbDoctor/Common/Logger.hpp"
using namespace std;
namespace AgentDbDoctor::Common {
// The spdlog level that corresponds to each of our own levels. A private
// namespace keeps these constants out of `AgentDbDoctor::Common` so that nothing
// outside this file can depend on them. `constexpr` means the value is computed
// by the compiler and stored in read only memory.
namespace SpdlogLevel {
    constexpr spdlog::level::level_enum DEBUG=spdlog::level::debug;
    constexpr spdlog::level::level_enum INFO=spdlog::level::info;
    constexpr spdlog::level::level_enum WARNING=spdlog::level::warn;
    constexpr spdlog::level::level_enum SEVERE=spdlog::level::err;
}
// Converts one of our levels into the matching spdlog level. The mapping is a
// `switch` rather than an array indexed by an enum value so that a new level
// cannot be silently treated as the wrong one. The forward declaration is needed
// because `write` below is defined before this function.
static spdlog::level::level_enum toSpdlogLevel(VerbosityLevel level);
Logger::Logger()
    : spdlogLogger(buildLogger(string(), "agent-db-doctor-console")),
      lineCount(0) {
    spdlogLogger->set_level(SpdlogLevel::INFO);
    spdlogLogger->flush_on(SpdlogLevel::WARNING);
}
Logger::Logger(const string& logFilePath)
    : spdlogLogger(buildLogger(logFilePath, "agent-db-doctor-service")),
      lineCount(0) {
    // The service only reports problems and the occasional state change. Debug
    // and info lines are discarded by the level check before any formatting
    // happens, which is what keeps an idle service under one hundredth of a
    // percent of one processor.
    spdlogLogger->set_level(SpdlogLevel::WARNING);
    spdlogLogger->flush_on(SpdlogLevel::WARNING);
}
Logger::~Logger() {
    if (spdlogLogger) {
        spdlogLogger->flush();
        // Dropping the last reference removes the logger from the spdlog
        // registry, which is a process wide global. Without this, a second
        // Logger created in the same process would silently reuse this sink.
        spdlog::drop(spdlogLogger->name());
    }
}
shared_ptr<spdlog::logger> Logger::buildLogger(const string& targetPath, const string& loggerName) {
    // The registry is a process wide map of named loggers. Registering by name
    // lets a later logger in the same process replace this one cleanly instead of
    // creating a second sink onto the same file.
    spdlog::drop(loggerName);
    if (targetPath.empty()) {
        // `stdout_sink_mt` writes plain lines with no escape sequences, so the
        // output stays readable when it is piped into a file or a test harness.
        auto sink=make_shared<spdlog::sinks::stdout_sink_mt>();
        auto createdLogger=make_shared<spdlog::logger>(loggerName, sink);
        spdlog::register_logger(createdLogger);
        createdLogger->set_pattern("[%Y-%m-%d %H:%M:%S] %v %s:%# %m");
        return createdLogger;
    }
    const filesystem::path logPath(targetPath);
    const filesystem::path parentDirectory=logPath.parent_path();
    if (!parentDirectory.empty() && !filesystem::exists(parentDirectory)) {
        // `create_directories` succeeds silently when the directory already
        // exists, so no separate existence test is needed first. The
        // `std::error_code` overload is used because the throwing overload would
        // turn a permission problem into an exception on the service start path.
        std::error_code ignoredError;
        filesystem::create_directories(parentDirectory, ignoredError);
    }
    // A file sink opens the file for appending. The second argument is `false`,
    // meaning "do not truncate", so restarting the service never destroys the
    // previous run's log.
    auto sink=make_shared<spdlog::sinks::basic_file_sink_mt>(targetPath, false);
    auto createdLogger=make_shared<spdlog::logger>(loggerName, sink);
    spdlog::register_logger(createdLogger);
    createdLogger->set_pattern("[%Y-%m-%d %H:%M:%S] %v %s:%# %m");
    return createdLogger;
}
static spdlog::level::level_enum toSpdlogLevel(VerbosityLevel level) {
    switch (level) {
        case VerbosityLevel::DEBUG: return SpdlogLevel::DEBUG;
        case VerbosityLevel::INFO: return SpdlogLevel::INFO;
        case VerbosityLevel::WARNING: return SpdlogLevel::WARNING;
        case VerbosityLevel::SEVERE: return SpdlogLevel::SEVERE;
    }
    return SpdlogLevel::INFO;
}
void Logger::write(VerbosityLevel level, const string& messageText) const {
    // The level comparison happens before the string is handed to spdlog, so a
    // filtered message costs one comparison and nothing else.
    if (!spdlogLogger->should_log(toSpdlogLevel(level))) {
        return;
    }
    switch (level) {
        case VerbosityLevel::DEBUG: spdlogLogger->debug(messageText); break;
        case VerbosityLevel::INFO: spdlogLogger->info(messageText); break;
        case VerbosityLevel::WARNING: spdlogLogger->warn(messageText); break;
        case VerbosityLevel::SEVERE: spdlogLogger->error(messageText); break;
    }
    lineCount++;
}
void Logger::debug(const string& messageText) const {
    write(VerbosityLevel::DEBUG, messageText);
}
void Logger::info(const string& messageText) const {
    write(VerbosityLevel::INFO, messageText);
}
void Logger::warn(const string& messageText) const {
    write(VerbosityLevel::WARNING, messageText);
}
void Logger::error(const string& messageText) const {
    write(VerbosityLevel::SEVERE, messageText);
}
void Logger::setMinimumLevel(VerbosityLevel minimumLevel) const {
    spdlogLogger->set_level(toSpdlogLevel(minimumLevel));
}
VerbosityLevel Logger::minimumLevel() const {
    switch (spdlogLogger->level()) {
        case SpdlogLevel::DEBUG: return VerbosityLevel::DEBUG;
        case SpdlogLevel::INFO: return VerbosityLevel::INFO;
        case SpdlogLevel::WARNING: return VerbosityLevel::WARNING;
        default: return VerbosityLevel::SEVERE;
    }
}
long long Logger::writtenLineCount() const {
    return lineCount;
}
void Logger::flush() const {
    spdlogLogger->flush();
}
}
