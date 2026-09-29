// The exception types raised by agent-db-doctor, and the translators that turn
// raw error codes into plain English sentences.
//
// Design note: the specification forbids inheritance anywhere in this codebase.
// C++ exceptions are often modelled as a class hierarchy, so these types use
// composition instead. Every exception holds an `AgentDbDoctorException` value
// as a private field and forwards `what()` and `message()` to it. Nothing
// derives from anything, yet `catch (const DatabaseOpenException& error)` still
// works exactly as a Java developer expects.
#ifndef AGENT_DB_DOCTOR_COMMON_EXCEPTIONS_HPP
#define AGENT_DB_DOCTOR_COMMON_EXCEPTIONS_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Common {
// The value carried by every agent-db-doctor exception.
//
// This class is not a base class. Nothing derives from it. It exists so that
// the concrete exception types can share one place to store and format the
// human readable explanation of a failure.
class AgentDbDoctorException final {
public:
    // Stores the explanation. The text must already be a complete sentence that
    // ends with a period, because every message the user ever sees follows that
    // rule.
    explicit AgentDbDoctorException(string explanation)
        : explanationText(move(explanation)) {
    }
    ~AgentDbDoctorException()=default;
    // Copying is deleted because the class has no reason to be copied and a
    // deleted copy makes accidental duplication a compile time error.
    AgentDbDoctorException(const AgentDbDoctorException&)=delete;
    AgentDbDoctorException& operator=(const AgentDbDoctorException&)=delete;
    // `what()` returns a pointer that stays valid for the lifetime of this
    // object. It is the same text as `message()`.
    const char* what() const {
        return explanationText.c_str();
    }
    // The complete plain English explanation of the failure.
    const string& message() const {
        return explanationText;
    }
private:
    string explanationText;
};
// The Translator classes turn numeric error codes into sentences.
//
// `SqliteErrorTranslator` covers the SQLite result codes documented at
// https://www.sqlite.org/rescode.html. `OperatingSystemErrorTranslator` covers
// the errno values on Linux and macOS and the Win32 error numbers on Windows.
// Both are used by the presentation layer and by the exception constructors, so
// a raw code is never shown to the user without a sentence beside it.
class SqliteErrorTranslator final {
public:
    SqliteErrorTranslator()=default;
    ~SqliteErrorTranslator()=default;
    SqliteErrorTranslator(const SqliteErrorTranslator&)=delete;
    SqliteErrorTranslator& operator=(const SqliteErrorTranslator&)=delete;
    // Returns a sentence explaining the SQLite result code. Unknown codes get a
    // sentence that still names the code, so the user always has something to
    // search for.
    static string explain(int sqliteResultCode);
    // Returns a short label for the code, for example "file is not a database".
    static string shortLabel(int sqliteResultCode);
private:
};
// The OperatingSystemErrorTranslator converts a platform error number into a
// sentence. On Windows the numbers come from `GetLastError`. On Linux and macOS
// they come from the `errno` variable.
class OperatingSystemErrorTranslator final {
public:
    OperatingSystemErrorTranslator()=default;
    ~OperatingSystemErrorTranslator()=default;
    OperatingSystemErrorTranslator(const OperatingSystemErrorTranslator&)=delete;
    OperatingSystemErrorTranslator& operator=(const OperatingSystemErrorTranslator&)=delete;
    // Returns a sentence explaining the operating system error code.
    static string explain(int operatingSystemErrorCode);
    // Returns the symbolic name of the code, for example "ERROR_READ_FAULT".
    static string symbolicName(int operatingSystemErrorCode);
private:
};
// The AgentDatabaseMissingException is raised when no supported coding agent
// could be found on this machine.
class AgentDatabaseMissingException final {
public:
    explicit AgentDatabaseMissingException(string explanation)
        : baseException(move(explanation)) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
private:
    AgentDbDoctorException baseException;
};
// The DatabaseOpenException is raised when SQLite refuses to open a database.
class DatabaseOpenException final {
public:
    // `databaseFilePath` is the path that failed. `sqliteResultCode` is the
    // value returned by `sqlite3_open_v2`. Both are translated into a sentence.
    DatabaseOpenException(string databaseFilePath, int sqliteResultCode, string explanation)
        : baseException(buildExplanation(move(databaseFilePath), sqliteResultCode, move(explanation))) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
    // The raw SQLite result code, so that a caller can branch on the specific
    // failure rather than parsing the sentence. The presentation layer uses the
    // sentence; the recovery layer uses this number.
    int openResultCode() const {
        return sqliteResultCode;
    }
private:
    // Builds the full sentence once, during construction, so that `what()` is a
    // trivial read on the error path.
    static string buildExplanation(const string& databaseFilePath, int sqliteResultCode, const string& explanation) {
        return "Could not open the database at " + databaseFilePath + ". "
            + SqliteErrorTranslator::explain(sqliteResultCode) + " " + explanation;
    }
    AgentDbDoctorException baseException;
    int sqliteResultCode=0;
};
// The DatabaseOperationException is raised when a statement fails for a reason
// other than opening the file.
class DatabaseOperationException final {
public:
    DatabaseOperationException(string operationDescription, int sqliteResultCode, string explanation)
        : baseException(buildExplanation(move(operationDescription), sqliteResultCode, move(explanation))) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
private:
    static string buildExplanation(const string& operationDescription, int sqliteResultCode, const string& explanation) {
        return operationDescription + " failed. " + SqliteErrorTranslator::explain(sqliteResultCode) + " " + explanation;
    }
    AgentDbDoctorException baseException;
};
// The PragmaException is raised when a PRAGMA statement cannot be applied.
class PragmaException final {
public:
    PragmaException(string pragmaStatement, string sqliteMessage)
        : baseException(buildExplanation(move(pragmaStatement), move(sqliteMessage))) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
private:
    static string buildExplanation(const string& pragmaStatement, const string& sqliteMessage) {
        return "Could not apply the setting " + pragmaStatement + ". SQLite said: " + sqliteMessage + ".";
    }
    AgentDbDoctorException baseException;
};
// The FileSystemException is raised when the operating system refuses a file
// operation.
class FileSystemException final {
public:
    FileSystemException(string operationDescription, string affectedPath, int operatingSystemErrorCode)
        : baseException(buildExplanation(move(operationDescription), move(affectedPath), operatingSystemErrorCode)) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
private:
    static string buildExplanation(const string& operationDescription, const string& affectedPath, int operatingSystemErrorCode) {
        return operationDescription + " failed for " + affectedPath + ". "
            + OperatingSystemErrorTranslator::explain(operatingSystemErrorCode);
    }
    AgentDbDoctorException baseException;
};
// The ConfigurationException is raised when the service configuration file is
// missing a required value or contains a value that cannot be used.
class ConfigurationException final {
public:
    explicit ConfigurationException(string explanation)
        : baseException("The configuration is not usable. " + move(explanation)) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
private:
    AgentDbDoctorException baseException;
};
// The ServiceOperationException is raised when the system service cannot be
// installed, started, stopped, or queried.
class ServiceOperationException final {
public:
    ServiceOperationException(string operationDescription, string explanation)
        : baseException(move(operationDescription) + " failed. " + move(explanation)) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
private:
    AgentDbDoctorException baseException;
};
// The UserDeclinedException is raised when the user answers "no" to a question
// that the tool asked before doing something irreversible. It is not a failure,
// so the message is written in a calm voice.
class UserDeclinedException final {
public:
    explicit UserDeclinedException(string explanation)
        : baseException(move(explanation)) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
private:
    AgentDbDoctorException baseException;
};
// The InsufficientDiskSpaceException is raised when an operation needs more free
// space than the volume currently has.
class InsufficientDiskSpaceException final {
public:
    InsufficientDiskSpaceException(string operationDescription, long long requiredBytes, long long availableBytes)
        : baseException(buildExplanation(move(operationDescription), requiredBytes, availableBytes)) {
    }
    const char* what() const {
        return baseException.what();
    }
    const string& message() const {
        return baseException.message();
    }
    // The number of bytes the operation needed. The report layer prints it so
    // the user can see exactly how much room to free.
    long long requiredBytes() const {
        return requiredByteCount;
    }
    // The number of bytes the volume actually has free.
    long long availableBytes() const {
        return availableByteCount;
    }
private:
    static string buildExplanation(const string& operationDescription, long long requiredBytes, long long availableBytes) {
        return operationDescription + " needs about " + to_string(requiredBytes)
            + " bytes of free disk space, but only " + to_string(availableBytes)
            + " bytes are free right now. Free some space, or move the database to a larger drive, and run the command again.";
    }
    AgentDbDoctorException baseException;
    long long requiredByteCount=0;
    long long availableByteCount=0;
};
}
#endif
