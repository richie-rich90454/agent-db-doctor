// The Statement class, a wrapper around one prepared SQLite statement.
//
// Preparing a statement separates parsing from running. `sqlite3_prepare_v2`
// turns SQL text into a program the database engine can execute, and the result
// can be run many times by resetting it and binding new values. Principle P-11
// of the specification says never prepare inside a loop, and this class exists
// to make that the natural thing to do.
#ifndef AGENT_DB_DOCTOR_PERSISTENCE_STATEMENT_HPP
#define AGENT_DB_DOCTOR_PERSISTENCE_STATEMENT_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `string_view`, a read only view of characters owned elsewhere.
#include <string_view>
// Provides the raw SQLite C API.
#include <sqlite3.h>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Persistence {
// The Statement class owns one prepared statement and the values bound to it.
//
// The class is `final` because the specification forbids inheritance. It does not
// own the connection: it borrows the `sqlite3*` that the Database class owns, so
// the connection must outlive every Statement made from it. That is always true
// inside the methods of Database, which is the only class that creates one.
class Statement final {
public:
    // Prepares `sqlStatement` on `databaseHandle`.
    //
    // `operationDescription` is a sentence used in error messages, so a failure
    // says what the tool was trying to do rather than showing only SQL text. A
    // statement that fails to prepare is not an exception: the class records the
    // failure and every later call becomes a safe no-op. That is what lets the
    // diagnostic code probe for optional tables without try and catch blocks.
    Statement(sqlite3* databaseHandle, const string& sqlStatement, string operationDescription);
    // Finalises the compiled statement. Without this the compiled program would
    // leak.
    ~Statement();
    // A statement owns a compiled program, so copying one would mean compiling
    // twice and finalising a handle that two objects both believe they own.
    Statement(const Statement&)=delete;
    Statement& operator=(const Statement&)=delete;
    Statement(Statement&&)=delete;
    Statement& operator=(Statement&&)=delete;
    // True when the SQL text compiled successfully.
    bool isValid() const;
    // The SQLite result code from a failed prepare, or zero on success.
    int prepareResultCode() const;
    // The sentence describing what this statement was for.
    const string& description() const;
    // Binds a 64 bit integer to the parameter at `parameterIndex`, where the
    // first parameter is number one. Parameters appear in the SQL text as `?1`,
    // `?2` and so on. Binding a value instead of pasting it into the text is what
    // makes a statement safe from injection, and what lets SQLite reuse the
    // compiled program across many rows.
    void bindInteger(int parameterIndex, long long value) const;
    // Binds text to a parameter.
    //
    // `value` is a `string_view`, a read only view of characters owned elsewhere,
    // so binding costs no allocation. The characters are copied by SQLite, which
    // is required because the caller's string may be destroyed before the
    // statement is stepped.
    void bindText(int parameterIndex, string_view value) const;
    // Binds nothing, which is how a parameter is explicitly set to SQL NULL.
    void bindNull(int parameterIndex) const;
    // Runs the statement one step.
    //
    // Returns true when a row is available, and false when there are no more rows
    // or when an error occurred. Running the statement again continues from where
    // it left off, which is how a loop over a large result set works. The return
    // value is deliberately not an error code, because a loop body should not
    // have to check for errors when it only wants to know whether another row
    // exists. Call `lastStepResultCode()` after the loop for that.
    bool step() const;
    // Runs a statement that does not produce rows, such as an insert, an update,
    // or a delete, and reports whether it succeeded.
    //
    // A statement that writes finishes by reporting that it is done rather than by
    // producing a row, so `step()` cannot be used for one. This method treats both
    // outcomes as success and anything else as a failure, which is the only thing
    // a write site wants to know.
    bool run() const;
    // The result code of the most recent `step()` or `run()` call.
    int lastStepResultCode() const;
    // Rewinds the statement so it can be run again from the first row. Parameter
    // bindings survive a reset, so a loop can change one value and repeat.
    void reset() const;
    // Returns the integer in the given column of the current row. Column numbers
    // start at zero. Reading a column when no row is current returns zero, because
    // the value comes from SQLite rather than from the caller.
    long long columnInteger(int columnIndex) const;
    // Returns the text in the given column of the current row, or the empty
    // string when the column holds nothing. The characters are copied, so the
    // result stays valid after the statement moves on.
    string columnText(int columnIndex) const;
    // Returns the text in the given column of the current row without copying it.
    //
    // The returned view points at memory owned by SQLite and becomes invalid at
    // the next `step()` or `reset()`. It is used on the hot path, where copying a
    // name for every one of millions of rows would dominate the cost.
    string_view columnTextView(int columnIndex) const;
    // Returns the number of columns the result has.
    int columnCount() const;
    // Returns the name of the given column, which is empty for an expression that
    // has no name.
    string columnName(int columnIndex) const;
    // True when the column at `columnIndex` holds nothing at all, which is
    // different from it holding an empty string.
    bool columnIsNull(int columnIndex) const;
    // The raw statement handle. No caller outside the Database class needs it.
    sqlite3_stmt* rawHandle() const;
private:
    // The compiled statement. It is a raw pointer because the finalise call is
    // written once in the destructor rather than through a `unique_ptr` with a
    // custom deleter, which would add a function pointer member to every
    // instance.
    sqlite3_stmt* rawStatementHandle=nullptr;
    // The sentence describing what this statement was for.
    string operationDescription;
    // The result code from a failed prepare.
    mutable int prepareFailureCode=SQLITE_OK;
    // The result code from the most recent `step()`.
    mutable int stepFailureCode=SQLITE_OK;
};
}
#endif
