// The Logger class, the single place the tool writes log lines.
//
// The specification permits spdlog and asks for an asynchronous sink backed by a
// pre allocated ring buffer. spdlog's own hierarchy of logger classes is a class
// hierarchy, and the specification forbids inheritance in this codebase, so this
// class does not derive from anything. It holds one spdlog logger as a field and
// forwards to it.
#ifndef AGENT_DB_DOCTOR_COMMON_LOGGER_HPP
#define AGENT_DB_DOCTOR_COMMON_LOGGER_HPP
// Provides `memory`, which owns a `shared_ptr`.
#include <memory>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the spdlog logger that this class wraps.
#include <spdlog/spdlog.h>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Common {
// The VerbosityLevel enum names the four levels the tool writes.
//
// An `enum class` is a strongly typed enumeration: its values do not silently
// convert to `int`, which prevents a mistyped argument from compiling. A Java
// developer would recognise this as a Java enum.
enum class VerbosityLevel {
    // Detailed tracing, enabled only by the `--verbose` flag.
    DEBUG,
    // Normal progress information.
    INFO,
    // Something unexpected happened but the operation continued.
    WARNING,
    // The operation could not continue.
    SEVERE
};
// The Logger class writes timestamped lines to a file and, optionally, to the
// console.
//
// The class is `final` because the specification forbids inheritance. It holds no
// mutable state of its own beyond the sink handle, so it is safe to share one
// instance across the single thread the tool uses.
class Logger final {
public:
    // Creates a logger that writes to standard output. Used by the command line
    // tool, where the user wants to see what is happening.
    Logger();
    // Creates a logger that writes to the file at `logFilePath`, creating the
    // parent directory if it does not exist. The file is held open for the
    // lifetime of the logger, which is what makes the service able to write one
    // line per hour with a single system call.
    explicit Logger(const string& logFilePath);
    ~Logger();
    // Copying is deleted because two loggers sharing one sink would interleave
    // their lines unpredictably.
    Logger(const Logger&)=delete;
    Logger& operator=(const Logger&)=delete;
    // Moving is allowed because `shared_ptr` transfers ownership cleanly.
    Logger(Logger&&) noexcept=default;
    Logger& operator=(Logger&&) noexcept=default;
    // Writes one line at the given level. `messageText` is a complete sentence.
    void write(VerbosityLevel level, const string& messageText) const;
    // Writes one line at the debug level.
    void debug(const string& messageText) const;
    // Writes one line at the info level.
    void info(const string& messageText) const;
    // Writes one line at the warning level.
    void warn(const string& messageText) const;
    // Writes one line at the error level.
    void error(const string& messageText) const;
    // Sets the lowest level that will be written. Messages below it are dropped
    // without being formatted, which is how the idle service stays at well under
    // one hundredth of a percent of one processor.
    void setMinimumLevel(VerbosityLevel minimumLevel) const;
    // The lowest level that will currently be written.
    VerbosityLevel minimumLevel() const;
    // The number of lines this logger has written. The benchmark suite reads it
    // to confirm the watchdog writes nothing while a database stays below the
    // configured threshold.
    long long writtenLineCount() const;
    // Flushes any buffered lines to the operating system. The service calls this
    // on shutdown; the hot path never calls it.
    void flush() const;
private:
    // Builds the shared spdlog logger. `targetPath` is the file to write to, or
    // the empty string to write to standard output.
    static shared_ptr<spdlog::logger> buildLogger(const string& targetPath, const string& loggerName);
    // `shared_ptr` is a smart pointer that counts how many owners there are and
    // releases the object when the last owner goes away. It is used here because
    // the sink is owned jointly by the logger and by the spdlog registry.
    shared_ptr<spdlog::logger> spdlogLogger;
    // A counter of written lines. It is a plain `long long` rather than an atomic
    // because the tool is single threaded by design. The `mutable` keyword lets a
    // `const` method update it, the same way a Java `AtomicLong` would, without
    // forcing every call site to drop `const`.
    mutable long long lineCount=0;
};
}
#endif
