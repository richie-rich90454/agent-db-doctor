// The implementation of the Database class.
//
// Every method here is the body of a method declared in Database.hpp. The
// separation is deliberate: it keeps SQLite's C style error handling out of the
// header, so a reader browsing the class interface sees only plain C++ types.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Database class declaration.
#include "AgentDbDoctor/Persistence/Database.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
namespace AgentDbDoctor::Persistence {
Database::Database(const string& databaseFilePath, int openFlags)
    : databaseFilePath(databaseFilePath) {
    sqlite3* rawDatabasePointer=nullptr;
    // `sqlite3_open_v2` opens the database and writes a handle into the pointer
    // we pass. It returns an integer result code, where zero means success.
    // Unlike the older `sqlite3_open`, it does not create the file unless
    // `SQLITE_OPEN_CREATE` is among the flags.
    const int openResult=sqlite3_open_v2(
        databaseFilePath.c_str(),
        &rawDatabasePointer,
        openFlags,
        nullptr
    );
    if (openResult!=SQLITE_OK) {
        // When the open fails, SQLite may still have allocated a handle, and the
        // documentation requires closing it. The message is read first, because
        // closing the handle frees it.
        string capturedMessage=lastMessageFrom(rawDatabasePointer);
        if (rawDatabasePointer!=nullptr) {
            sqlite3_close(rawDatabasePointer);
        }
        throw DatabaseOpenException(
            databaseFilePath,
            openResult,
            capturedMessage
                + " The file may not exist, may not be a database, may be locked by another program, or may be damaged."
        );
    }
    databaseHandle=unique_ptr<sqlite3, decltype(&sqlite3_close)>(
        rawDatabasePointer,
        sqlite3_close
    );
}
string Database::lastMessageFrom(sqlite3* handle) {
    if (handle==nullptr) {
        return string("SQLite could not even allocate a connection.");
    }
    const char* message=sqlite3_errmsg(handle);
    return message!=nullptr ? string(message) : string("SQLite gave no further detail.");
}
void Database::applyPragma(const char* pragmaStatement) const {
    char* errorMessage=nullptr;
    // `sqlite3_exec` runs one or more statements that return no rows. It is the
    // right call for pragmas that only change a setting. The third argument is a
    // callback invoked for each result row, and `nullptr` means "I do not want
    // the rows". The fifth argument receives a message string SQLite allocates
    // when something fails.
    const int executeResult=sqlite3_exec(
        databaseHandle.get(),
        pragmaStatement,
        nullptr,
        nullptr,
        &errorMessage
    );
    if (executeResult!=SQLITE_OK) {
        // SQLite allocated the message, so it must be copied and then released
        // with `sqlite3_free`. Using the C++ allocator for that free would be a
        // bug, because the two allocators are not interchangeable.
        const string capturedMessage=errorMessage!=nullptr
            ? string(errorMessage)
            : string("SQLite gave no further detail.");
        sqlite3_free(errorMessage);
        throw PragmaException(string(pragmaStatement), capturedMessage);
    }
    // On success SQLite may still set the message pointer, so it is always freed.
    sqlite3_free(errorMessage);
}
optional<long long> Database::queryPragmaInteger(const char* pragmaStatement) const {
    Statement integerStatement(databaseHandle.get(), pragmaStatement, string("the pragma ") + pragmaStatement);
    if (!integerStatement.isValid() || !integerStatement.step()) {
        return nullopt;
    }
    return integerStatement.columnInteger(0);
}
optional<string> Database::queryPragmaText(const char* pragmaStatement) const {
    Statement textStatement(databaseHandle.get(), pragmaStatement, string("the pragma ") + pragmaStatement);
    if (!textStatement.isValid() || !textStatement.step()) {
        return nullopt;
    }
    return textStatement.columnText(0);
}
void Database::execute(const string& sqlStatement) const {
    char* errorMessage=nullptr;
    const int executeResult=sqlite3_exec(
        databaseHandle.get(),
        sqlStatement.c_str(),
        nullptr,
        nullptr,
        &errorMessage
    );
    if (executeResult!=SQLITE_OK) {
        const string capturedMessage=errorMessage!=nullptr
            ? string(errorMessage)
            : string("SQLite gave no further detail.");
        sqlite3_free(errorMessage);
        throw DatabaseOperationException(
            string("The statement ") + sqlStatement,
            executeResult,
            capturedMessage
        );
    }
    sqlite3_free(errorMessage);
}
optional<long long> Database::querySingleInteger(const string& sqlStatement) const {
    Statement countingStatement(databaseHandle.get(), sqlStatement, string("the query ") + sqlStatement);
    if (!countingStatement.isValid() || !countingStatement.step()) {
        return nullopt;
    }
    return countingStatement.columnInteger(0);
}
optional<string> Database::querySingleText(const string& sqlStatement) const {
    Statement textStatement(databaseHandle.get(), sqlStatement, string("the query ") + sqlStatement);
    if (!textStatement.isValid() || !textStatement.step()) {
        return nullopt;
    }
    return textStatement.columnText(0);
}
long long Database::lastChangedRowCount() const {
    // `sqlite3_changes64` returns the number of rows the most recently completed
    // statement inserted, updated, or deleted.
    return sqlite3_changes64(databaseHandle.get());
}
long long Database::lastInsertedRowIdentifier() const {
    return sqlite3_last_insert_rowid(databaseHandle.get());
}
void Database::setBusyTimeoutMilliseconds(int timeoutMilliseconds) const {
    // `sqlite3_busy_timeout` installs a handler that sleeps and retries when the
    // database is locked, up to the given number of milliseconds. With the
    // vendored build compiled single threaded there is no mutex to wait on, so
    // this only affects the file lock that another process holds.
    sqlite3_busy_timeout(databaseHandle.get(), timeoutMilliseconds);
}
int Database::lastResultCode() const {
    return sqlite3_errcode(databaseHandle.get());
}
string Database::lastErrorMessage() const {
    const char* message=sqlite3_errmsg(databaseHandle.get());
    return message!=nullptr ? string(message) : string("SQLite gave no further detail.");
}
const string& Database::filePath() const {
    return databaseFilePath;
}
bool Database::isReadOnly() const {
    if (databaseHandle==nullptr) {
        return true;
    }
    // `sqlite3_db_readonly` reports whether the database was opened without write
    // permission. It takes a schema name, and the empty string means "the main
    // database".
    return sqlite3_db_readonly(databaseHandle.get(), "main")==1;
}
sqlite3* Database::rawHandle() const {
    return databaseHandle.get();
}
}
