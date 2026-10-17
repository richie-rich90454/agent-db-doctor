// The Database class, a wrapper around one SQLite connection.
//
// This is the only class in the codebase that holds a raw `sqlite3*`. Every
// other class asks this one to run statements and never touches the pointer. The
// wrapper exists for three reasons: it closes the handle automatically, it turns
// SQLite result codes into sentences, and it keeps application code free of the
// resource acquisition and release pattern.
#ifndef AGENT_DB_DOCTOR_PERSISTENCE_DATABASE_HPP
#define AGENT_DB_DOCTOR_PERSISTENCE_DATABASE_HPP
// Provides `memory`, which owns `unique_ptr`.
#include <memory>
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Statement class, used to run pragmas and constant time queries.
#include "AgentDbDoctor/Persistence/Statement.hpp"
// Provides the exception types raised when an operation fails.
#include "AgentDbDoctor/Common/Exceptions.hpp"
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Persistence {
// The Database class wraps an SQLite connection in a resource acquiring
// container.
//
// The class is `final` because the specification forbids inheritance. Shared
// behaviour is achieved through composition: this class holds a connection and
// constructs Statement objects from it, it does not extend anything and nothing
// extends it.
class Database final {
public:
    // Open modes. These are the flags defined by SQLite, given readable names so
    // that call sites read like Java named constants rather than magic numbers.
    // The values come from https://www.sqlite.org/c3ref/open.html.
    static constexpr int OPEN_READ_ONLY=SQLITE_OPEN_READONLY;
    static constexpr int OPEN_READ_WRITE=SQLITE_OPEN_READWRITE;
    static constexpr int OPEN_CREATE=SQLITE_OPEN_CREATE;
    // `SQLITE_OPEN_URI` lets a path that begins with `file:` be read as a SQLite
    // URI. That is how the tool asks for an explicitly immutable connection
    // during recovery, so SQLite never tries to write a recovery journal.
    static constexpr int OPEN_URI=SQLITE_OPEN_URI;
    // `SQLITE_OPEN_NOMUTEX` tells SQLite not to guard this connection with a
    // mutex. The tool is single threaded by design, so the mutex would be pure
    // overhead. The flag is accepted for documentation value only, because the
    // vendored SQLite is compiled with `SQLITE_THREADSAFE=0` and therefore has
    // no mutexes at all.
    static constexpr int OPEN_NO_MUTEX=SQLITE_OPEN_NOMUTEX;
    // Opens a database at the given path.
    //
    // `databaseFilePath` is the path on disk. `openFlags` is a combination of the
    // constants above. The constructor throws `DatabaseOpenException` when SQLite
    // refuses the file, because a caller that cannot open a database has nothing
    // useful to do.
    explicit Database(const string& databaseFilePath, int openFlags);
    // A default constructed database would have no connection, so the constructor
    // is deleted rather than left to fail at run time.
    Database()=delete;
    // The destructor is defaulted. The `unique_ptr` field closes the connection
    // automatically when the object is destroyed.
    ~Database()=default;
    // Copying is forbidden, because two objects owning one connection would close
    // the same handle twice.
    Database(const Database&)=delete;
    Database& operator=(const Database&)=delete;
    // Moving is permitted, because `unique_ptr` transfers ownership cleanly and
    // the moved from object is left owning nothing.
    Database(Database&& other) noexcept=default;
    Database& operator=(Database&& other) noexcept=default;
    // Applies a PRAGMA statement to the connection, for example
    // `PRAGMA synchronous = NORMAL`. Throws `PragmaException` on failure.
    void applyPragma(const char* pragmaStatement) const;
    // Applies a PRAGMA and reads back the single integer it returns, for example
    // `PRAGMA page_size`. Returns `nullopt` when the pragma cannot be run,
    // because a caller can usually live without the value.
    optional<long long> queryPragmaInteger(const char* pragmaStatement) const;
    // Applies a PRAGMA and reads back the single text value it returns, for
    // example `PRAGMA journal_mode`. Returns `nullopt` when it cannot be run.
    optional<string> queryPragmaText(const char* pragmaStatement) const;
    // Runs a statement that returns no rows, such as an `INSERT` or a `DELETE`.
    // Throws `DatabaseOperationException` on failure.
    void execute(const string& sqlStatement) const;
    // Runs a query and returns the first column of the first row, or `nullopt`
    // when the query fails or returns no rows. Used for the constant time lookups
    // the tool performs, such as reading the schema version.
    optional<long long> querySingleInteger(const string& sqlStatement) const;
    // Runs a query and returns the first column of the first row as text, or
    // `nullopt` when the query fails or returns no rows.
    optional<string> querySingleText(const string& sqlStatement) const;
    // The number of rows the last statement changed, meaning rows inserted,
    // updated, or deleted. Returns negative one when the statement changed
    // nothing.
    long long lastChangedRowCount() const;
    // The row identifier SQLite assigned to the most recent inserted row.
    long long lastInsertedRowIdentifier() const;
    // Tells SQLite how long to wait for a lock before giving up. The default of
    // zero means "fail immediately", which is unhelpful while a coding agent is
    // running, so the tool always sets a short wait before doing real work.
    void setBusyTimeoutMilliseconds(int timeoutMilliseconds) const;
    // The result code of the most recent call on this connection.
    int lastResultCode() const;
    // The message SQLite produced for the most recent failure.
    string lastErrorMessage() const;
    // The path this connection was opened from, exactly as it was given.
    const string& filePath() const;
    // True when the connection was opened for reading only.
    bool isReadOnly() const;
    // Returns the raw handle for use by the Statement class. This is the only
    // place a raw pointer leaves this class, and only the Statement class uses
    // it. No other part of the tool ever sees an `sqlite3*`.
    sqlite3* rawHandle() const;
private:
    // Reads the message SQLite produced for a handle that failed to open. The
    // handle is about to be closed, so the string must be copied before that.
    static string lastMessageFrom(sqlite3* handle);
    // The path this connection was opened from.
    string databaseFilePath;
    // `unique_ptr` is a smart pointer that owns the object it points to and
    // releases it automatically when it goes out of scope. The second template
    // argument names the release function, which is how the SQLite connection is
    // closed without anyone writing `delete` or remembering to call
    // `sqlite3_close`. This is the resource acquisition is initialisation
    // pattern: the handle is owned from the instant it exists, so no code path
    // can leak it.
    unique_ptr<sqlite3, decltype(&sqlite3_close)> databaseHandle{nullptr, sqlite3_close};
};
}
#endif
