// The implementation of the Statement class.
//
// Every method here is the body of a method declared in Statement.hpp. The
// separator is a single blank line because this file is a sequence of short
// methods with no shared state between them.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `string_view`, a read only view of characters owned elsewhere.
#include <string_view>
// Provides the Statement class declaration.
#include "AgentDbDoctor/Persistence/Statement.hpp"
using namespace std;
using namespace AgentDbDoctor;
namespace AgentDbDoctor::Persistence {
Statement::Statement(sqlite3* databaseHandle, const string& sqlStatement, string operationDescription)
    : operationDescription(move(operationDescription)) {
    // The third argument asks SQLite to also report where in the SQL text it
    // stopped. The tool does not use that offset, so `nullptr` is passed. The
    // length of `-1` means "the text is null terminated, work it out yourself".
    const int prepareResult=sqlite3_prepare_v2(
        databaseHandle,
        sqlStatement.c_str(),
        -1,
        &rawStatementHandle,
        nullptr
    );
    if (prepareResult!=SQLITE_OK) {
        // The result code is kept so a caller can read it later, and the handle is
        // cleared so no later method can use a half built program.
        prepareFailureCode=prepareResult;
        rawStatementHandle=nullptr;
    }
}
Statement::~Statement() {
    // `sqlite3_finalize` releases the compiled program. The null check matters
    // because a statement that failed to prepare never got one.
    if (rawStatementHandle!=nullptr) {
        sqlite3_finalize(rawStatementHandle);
        rawStatementHandle=nullptr;
    }
}
bool Statement::isValid() const {
    return rawStatementHandle!=nullptr;
}
int Statement::prepareResultCode() const {
    return prepareFailureCode;
}
const string& Statement::description() const {
    return operationDescription;
}
void Statement::bindInteger(int parameterIndex, long long value) const {
    if (rawStatementHandle==nullptr) {
        return;
    }
    // `sqlite3_bind_int64` stores the value in the parameter slot. The value is
    // copied into SQLite's own memory, so the caller's variable may change or go
    // out of scope before the statement runs.
    sqlite3_bind_int64(rawStatementHandle, parameterIndex, value);
}
void Statement::bindText(int parameterIndex, string_view value) const {
    if (rawStatementHandle==nullptr) {
        return;
    }
    // `reinterpret_cast` reinterprets the bits of one type as another type. The
    // bytes behind a `string_view`'s data are already `char`, so this cast only
    // changes the type label that SQLite's declaration expects.
    //
    // `SQLITE_TRANSIENT` tells SQLite to make its own private copy of the
    // characters. That is required, because the caller's string may be destroyed
    // before the statement is stepped, and without a copy SQLite would be left
    // holding a pointer into freed memory. The alternative, `SQLITE_STATIC`,
    // promises the memory will outlive the statement, which this class cannot
    // promise on the caller's behalf.
    sqlite3_bind_text(
        rawStatementHandle,
        parameterIndex,
        value.data(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
    );
}
void Statement::bindNull(int parameterIndex) const {
    if (rawStatementHandle==nullptr) {
        return;
    }
    sqlite3_bind_null(rawStatementHandle, parameterIndex);
}
bool Statement::step() const {
    if (rawStatementHandle==nullptr) {
        return false;
    }
    // `sqlite3_step` runs the statement until it produces a row, runs out of rows,
    // or fails. `SQLITE_ROW` means a row is available now, `SQLITE_DONE` means
    // the statement finished normally.
    const int stepResult=sqlite3_step(rawStatementHandle);
    if (stepResult==SQLITE_ROW) {
        return true;
    }
    stepFailureCode=stepResult;
    return false;
}
bool Statement::run() const {
    if (rawStatementHandle==nullptr) {
        return false;
    }
    // A write finishes with the done code. A pragma that returns a value finishes
    // the same way, so both are treated as success here.
    const int runResult=sqlite3_step(rawStatementHandle);
    stepFailureCode=runResult;
    return runResult==SQLITE_DONE || runResult==SQLITE_ROW;
}
int Statement::lastStepResultCode() const {
    return stepFailureCode;
}
void Statement::reset() const {
    if (rawStatementHandle!=nullptr) {
        sqlite3_reset(rawStatementHandle);
    }
}
long long Statement::columnInteger(int columnIndex) const {
    if (rawStatementHandle==nullptr) {
        return 0;
    }
    return sqlite3_column_int64(rawStatementHandle, columnIndex);
}
string Statement::columnText(int columnIndex) const {
    if (rawStatementHandle==nullptr) {
        return string();
    }
    const unsigned char* rawText=sqlite3_column_text(rawStatementHandle, columnIndex);
    if (rawText==nullptr) {
        return string();
    }
    // The pointer returned by `sqlite3_column_text` belongs to SQLite and stays
    // valid only until the next step, reset, or finalise. The `string` constructor
    // copies the characters, so the result is safe to keep.
    return string(reinterpret_cast<const char*>(rawText));
}
string_view Statement::columnTextView(int columnIndex) const {
    if (rawStatementHandle==nullptr) {
        return string_view();
    }
    const unsigned char* rawText=sqlite3_column_text(rawStatementHandle, columnIndex);
    if (rawText==nullptr) {
        return string_view();
    }
    // `sqlite3_column_bytes` reports the length in bytes of the text that
    // `sqlite3_column_text` just returned. Reading it after the text pointer is
    // required by SQLite's rules, which is why it is not called before.
    const int byteCount=sqlite3_column_bytes(rawStatementHandle, columnIndex);
    return string_view(
        reinterpret_cast<const char*>(rawText),
        static_cast<size_t>(byteCount)
    );
}
int Statement::columnCount() const {
    if (rawStatementHandle==nullptr) {
        return 0;
    }
    return sqlite3_column_count(rawStatementHandle);
}
string Statement::columnName(int columnIndex) const {
    if (rawStatementHandle==nullptr) {
        return string();
    }
    const char* rawName=sqlite3_column_name(rawStatementHandle, columnIndex);
    return rawName!=nullptr ? string(rawName) : string();
}
bool Statement::columnIsNull(int columnIndex) const {
    if (rawStatementHandle==nullptr) {
        return true;
    }
    return sqlite3_column_type(rawStatementHandle, columnIndex)==SQLITE_NULL;
}
sqlite3_stmt* Statement::rawHandle() const {
    return rawStatementHandle;
}
}
