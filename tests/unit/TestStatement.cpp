// The unit tests for the Statement class on its own.
//
// The Database tests cover the two classes together. This file covers the parts of
// the statement interface that only matter on their own: reading a column without
// copying it, and telling a null column from an empty one.
#include <string>
// Provides `string_view`, the read only view a column can be read as.
#include <string_view>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the Database and Statement classes under test.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("a column can be read without copying it", "[statement]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStatementView");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    database.execute("CREATE TABLE names (body TEXT)");
    database.execute("INSERT INTO names (body) VALUES ('a name long enough to be worth copying')");
    Statement readingStatement(
        database.rawHandle(),
        "SELECT body FROM names",
        "a read of one name"
    );
    REQUIRE(readingStatement.isValid());
    REQUIRE(readingStatement.columnCount()==1);
    REQUIRE(readingStatement.columnName(0)=="body");
    REQUIRE(readingStatement.step());
    // The view points at memory SQLite owns, so it costs no allocation. It is valid
    // until the statement moves on, which is why the copy is taken before the step.
    const string_view nameView=readingStatement.columnTextView(0);
    REQUIRE(nameView=="a name long enough to be worth copying");
    // The copied form is the same text and outlives the row.
    const string nameCopy=readingStatement.columnText(0);
    REQUIRE(readingStatement.step()==false);
    REQUIRE(nameCopy=="a name long enough to be worth copying");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("an empty column is told apart from a missing one", "[statement]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStatementNull");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    database.execute("CREATE TABLE names (body TEXT)");
    database.execute("INSERT INTO names (body) VALUES ('')");
    database.execute("INSERT INTO names (body) VALUES (NULL)");
    Statement readingStatement(
        database.rawHandle(),
        "SELECT body FROM names ORDER BY rowid",
        "a read of an empty column and a missing one"
    );
    REQUIRE(readingStatement.isValid());
    REQUIRE(readingStatement.step());
    // The first row holds an empty string, which is a value.
    REQUIRE_FALSE(readingStatement.columnIsNull(0));
    REQUIRE(readingStatement.columnText(0).empty());
    REQUIRE(readingStatement.step());
    // The second row holds nothing at all, which is not a value. The tool has to be
    // able to tell them apart, or a missing row would look like empty content.
    REQUIRE(readingStatement.columnIsNull(0));
    REQUIRE(readingStatement.columnText(0).empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a statement describes itself for error messages", "[statement]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStatementDescription");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    Statement describedStatement(
        database.rawHandle(),
        "SELECT 1",
        "a description used in a failure message"
    );
    // The description is what a user sees when something fails, so it must be the
    // phrase the caller supplied rather than the SQL text.
    REQUIRE(describedStatement.description()=="a description used in a failure message");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
