// The unit tests for the SchemaVersionDetector class.
//
// Each case builds a real SQLite file with a different generation of content
// and checks the detector names it. The point of the suite is the contract:
// rows decide, never the file name, and unrecognised rows refuse rather than
// pass as something they are not.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Catch2 test macros.
#include <catch2/catch_test_macros.hpp>
// Provides the SchemaVersionDetector class under test.
#include "AgentDbDoctor/Doctor/SchemaVersionDetector.hpp"
// Provides the Database wrapper the detector reads.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the synthetic database builder, so the tests work on real files.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("a database with only first generation rows is version one", "[schema]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("SchemaV1");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=2;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database database(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE(SchemaVersionDetector::detect(database)==SchemaGeneration::Version1);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a database with both generations is version one and two", "[schema]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("SchemaV1V2");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=1;
    options.messageCountPerSession=1;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database writer(databasePath, Database::OPEN_READ_WRITE);
    writer.execute("CREATE TABLE session_message (id TEXT PRIMARY KEY, session_id TEXT)");
    writer.execute("INSERT INTO session_message (id, session_id) VALUES ('msg-next-1', 'ses-1')");
    writer.execute(
        "INSERT INTO event (id, aggregate_id, seq, type, time_created, data, filler) "
        "VALUES ('evt-next-1', 'ses-1', 1, 'session.next.prompted', 1, '{}', 'x')");
    Database database(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE(SchemaVersionDetector::detect(database)==SchemaGeneration::Version1And2);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a database with only second generation rows is version two only", "[schema]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("SchemaV2");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=1;
    options.messageCountPerSession=1;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database writer(databasePath, Database::OPEN_READ_WRITE);
    writer.execute("DELETE FROM event");
    writer.execute("CREATE TABLE session_input (id TEXT PRIMARY KEY, session_id TEXT)");
    writer.execute("INSERT INTO session_input (id, session_id) VALUES ('in-next-1', 'ses-1')");
    Database database(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE(SchemaVersionDetector::detect(database)==SchemaGeneration::Version2Only);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("an empty event table on a sound schema is version one", "[schema]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("SchemaEmpty");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=1;
    options.messageCountPerSession=1;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database writer(databasePath, Database::OPEN_READ_WRITE);
    writer.execute("DELETE FROM event");
    Database database(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE(SchemaVersionDetector::detect(database)==SchemaGeneration::Version1);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("event rows of no known generation are unknown", "[schema]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("SchemaUnknown");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=1;
    options.messageCountPerSession=1;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database writer(databasePath, Database::OPEN_READ_WRITE);
    writer.execute("DELETE FROM event");
    writer.execute(
        "INSERT INTO event (id, aggregate_id, seq, type, time_created, data, filler) "
        "VALUES ('evt-future-1', 'ses-1', 1, 'session.future.something', 1, '{}', 'x')");
    Database database(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE(SchemaVersionDetector::detect(database)==SchemaGeneration::Unknown);
    REQUIRE_FALSE(SchemaVersionDetector::describe(SchemaGeneration::Unknown).empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("every generation has words for a report", "[schema]") {
    REQUIRE_FALSE(SchemaVersionDetector::describe(SchemaGeneration::Version1).empty());
    REQUIRE_FALSE(SchemaVersionDetector::describe(SchemaGeneration::Version1And2).empty());
    REQUIRE_FALSE(SchemaVersionDetector::describe(SchemaGeneration::Version2Only).empty());
}
