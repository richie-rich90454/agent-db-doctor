// The unit tests for the Database and Statement classes.
//
// These two are the only code in the tool that touches the SQLite C interface, so
// the tests here are about the wrapper behaving correctly in the cases the rest of
// the tool relies on: that a failure is reported rather than returned as a code, and
// that a statement can be prepared once and run many times.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the Database and Statement classes under test.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the exception types the wrapper raises.
#include "AgentDbDoctor/Common/Exceptions.hpp"
// Provides the synthetic database builder, so the tests work on real files.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("a database can be created, written to, and read back", "[database]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestDatabaseCreate");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    {
        Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
        database.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT)");
        database.execute("INSERT INTO notes (body) VALUES ('the first note')");
        const long long insertedIdentifier=database.lastInsertedRowIdentifier();
        REQUIRE(insertedIdentifier==1);
        REQUIRE(database.lastChangedRowCount()==1);
    }
    {
        // The file is reopened, which is what proves the data was actually written
        // rather than only held in memory.
        Database reopenedDatabase(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(reopenedDatabase.isReadOnly());
        const optional<string> storedBody=reopenedDatabase.querySingleText(
            "SELECT body FROM notes WHERE id=1"
        );
        REQUIRE(storedBody.has_value());
        REQUIRE(storedBody.value()=="the first note");
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("opening a missing file read only is refused with a sentence", "[database]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestDatabaseMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    // The refusal must be an exception carrying a complete sentence, not a code the
    // caller has to translate itself.
    REQUIRE_THROWS_AS(
        Database(missingPath, Database::OPEN_READ_ONLY),
        DatabaseOpenException
    );
    try {
        Database failingDatabase(missingPath, Database::OPEN_READ_ONLY);
        FAIL("the open should have been refused");
    } catch (const DatabaseOpenException& openFailure) {
        // The sentence names the file, so a user knows which one failed.
        REQUIRE(openFailure.message().find(missingPath)!=string::npos);
        REQUIRE(openFailure.message().back()=='.');
        // The raw code is available for a caller that needs to branch on it.
        REQUIRE(openFailure.openResultCode()!=0);
    }
}
TEST_CASE("a statement that will not compile is reported rather than thrown", "[statement]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStatementPrepare");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    // A query against a table that does not exist fails at prepare time. The
    // Statement class records that rather than throwing, which is what lets the
    // diagnostic code probe for optional tables without try and catch.
    Statement missingTableStatement(
        database.rawHandle(),
        "SELECT id FROM a_table_that_does_not_exist",
        "a query against a table that is not there"
    );
    REQUIRE_FALSE(missingTableStatement.isValid());
    REQUIRE(missingTableStatement.prepareResultCode()!=0);
    // Every other call is a safe no-op, so the caller does not have to check first.
    missingTableStatement.bindInteger(1, 1);
    REQUIRE_FALSE(missingTableStatement.step());
    REQUIRE(missingTableStatement.columnInteger(0)==0);
    REQUIRE(missingTableStatement.columnText(0).empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a prepared statement is run many times with new values", "[statement]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStatementReuse");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    database.execute("CREATE TABLE numbers (value INTEGER)");
    // The point of a prepared statement is that the SQL is parsed once. It is
    // prepared once here and stepped five times, which is exactly the pattern the
    // prune command uses for its batches.
    Statement insertStatement(
        database.rawHandle(),
        "INSERT INTO numbers (value) VALUES (?1)",
        "the writing of one number"
    );
    REQUIRE(insertStatement.isValid());
    for (int valueIndex=1; valueIndex<=5; ++valueIndex) {
        insertStatement.reset();
        insertStatement.bindInteger(1, valueIndex);
        REQUIRE(insertStatement.run());
    }
    const optional<long long> rowCount=database.querySingleInteger("SELECT COUNT(*) FROM numbers");
    REQUIRE(rowCount.has_value());
    REQUIRE(rowCount.value()==5);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a bound parameter is copied so the caller may change it", "[statement]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStatementBinding");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    database.execute("CREATE TABLE words (body TEXT)");
    Statement insertStatement(
        database.rawHandle(),
        "INSERT INTO words (body) VALUES (?1)",
        "the writing of one word"
    );
    REQUIRE(insertStatement.isValid());
    // The text is passed as a view over a string the caller then destroys. SQLite
    // made its own copy at bind time, so the row is not affected.
    {
        string transientWord="a value that will be destroyed immediately";
        insertStatement.bindText(1, transientWord);
    }
    REQUIRE(insertStatement.run());
    const optional<string> storedWord=database.querySingleText("SELECT body FROM words");
    REQUIRE(storedWord.has_value());
    REQUIRE(storedWord.value()=="a value that will be destroyed immediately");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a pragma that returns a value is read back", "[database]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestPragma");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    // Every SQLite database has a page size, and the pragma reports it as a single
    // integer. This is one of the constant time reads the tool depends on.
    const optional<long long> reportedPageSize=database.queryPragmaInteger("PRAGMA page_size");
    REQUIRE(reportedPageSize.has_value());
    REQUIRE(reportedPageSize.value()>0);
    // A pragma that does not exist returns nothing rather than failing.
    REQUIRE_FALSE(database.queryPragmaInteger("PRAGMA not_a_real_pragma").has_value());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a failing statement raises an exception with a sentence", "[database]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestDatabaseExecute");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    // Running a statement against a missing table fails, and the wrapper turns the
    // result code into a sentence rather than returning it.
    REQUIRE_THROWS_AS(
        database.execute("SELECT * FROM a_table_that_does_not_exist"),
        DatabaseOperationException
    );
    try {
        database.execute("SELECT * FROM a_table_that_does_not_exist");
        FAIL("the statement should have failed");
    } catch (const DatabaseOperationException& operationFailure) {
        REQUIRE(operationFailure.message().find('.')!=string::npos);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
