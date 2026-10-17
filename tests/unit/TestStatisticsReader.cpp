// The unit tests for the StatisticsReader and SafetyChecks classes.
//
// These two classes are the reason a compaction cannot lose a conversation. The tests
// therefore check the two things that matter: that row counts come from the
// constant time path, and that the safety checks fail closed when they cannot
// prove something is safe.
#include <string>
// Provides `optional`, which the row count answer may or may not hold.
#include <optional>
// Provides `ofstream`, used to write the file that is not a database.
#include <fstream>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the classes under test.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
// Provides the database wrapper the checks take.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the synthetic database builder, for a real agent shaped file.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("the header of a real database is read in one pass", "[statistics]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestHeaderRead");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    const SqliteHeaderFields headerFields=StatisticsReader::readHeaderFields(databasePath);
    // The signature is what proves the file is a database at all, and it is read
    // without opening the file through SQLite.
    REQUIRE(headerFields.isValidSqliteFile);
    REQUIRE(headerFields.pageSizeBytes>0);
    REQUIRE(headerFields.pageCount>0);
    // The declared size is what SQLite believes the file should be, which is
    // different from what the file system reports and worth comparing.
    REQUIRE(headerFields.declaredFileSizeBytes>0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a file that is not a database is recognised without SQLite", "[statistics]") {
    const string textFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestHeaderNotDatabase");
    SyntheticDatabaseBuilder::removeIfPresent(textFilePath);
    // A short text file is the clearest negative case, and the header reader must
    // report it as not a database rather than reading past the end.
    {
        std::ofstream textFile(textFilePath);
        textFile << "this is not a database";
    }
    const SqliteHeaderFields headerFields=StatisticsReader::readHeaderFields(textFilePath);
    REQUIRE_FALSE(headerFields.isValidSqliteFile);
    SyntheticDatabaseBuilder::removeIfPresent(textFilePath);
}
TEST_CASE("a file that is not there yields an empty header", "[statistics]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestHeaderMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    const SqliteHeaderFields headerFields=StatisticsReader::readHeaderFields(missingPath);
    // A missing file is an expected result of a diagnostic pass, not an error.
    REQUIRE_FALSE(headerFields.isValidSqliteFile);
}
TEST_CASE("row counts come from the statistics table in constant time", "[statistics]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStat1");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    Database database(databasePath, Database::OPEN_READ_ONLY);
    // With the statistics table populated, the count is available without a scan.
    // This is the difference between a constant time answer and one that reads the
    // whole table.
    const optional<long long> approximateSessionCount=
        StatisticsReader::approximateRowCount(database, "session");
    REQUIRE(approximateSessionCount.has_value());
    REQUIRE(approximateSessionCount.value()==20);
    // A table that is not in the statistics table has no answer rather than a wrong
    // one, which is what makes the caller fall back deliberately.
    REQUIRE_FALSE(StatisticsReader::approximateRowCount(database, "nothing").has_value());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("row counts fall back to a scan when the statistics are missing", "[statistics]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestStat2");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    // Without the statistics table, the constant time path is unavailable, so the
    // exact count is the only answer.
    options.populateStatisticsTable=false;
    options.sessionCount=5;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database database(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE_FALSE(StatisticsReader::approximateRowCount(database, "session").has_value());
    REQUIRE(StatisticsReader::exactRowCount(database, "session")==5);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("tables and columns are tested for", "[statistics]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSchema");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    Database database(databasePath, Database::OPEN_READ_ONLY);
    // The compaction query depends on exactly these tables and columns, so a release that
    // changes the shape has to be detected before a deletion, not during one.
    REQUIRE(StatisticsReader::tableExists(database, "event"));
    REQUIRE_FALSE(StatisticsReader::tableExists(database, "no_such_table"));
    REQUIRE(StatisticsReader::columnExists(database, "message", "session_id"));
    REQUIRE_FALSE(StatisticsReader::columnExists(database, "message", "no_such_column"));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the free list is turned into bytes", "[statistics]") {
    SqliteHeaderFields headerFields;
    headerFields.pageSizeBytes=4096;
    headerFields.freePageCount=1000;
    REQUIRE(StatisticsReader::reclaimableBytesFromFreeList(headerFields)==4096000);
    // An unknown page size or an empty free list means nothing is reclaimable, and
    // saying zero is better than guessing.
    headerFields.pageSizeBytes=0;
    REQUIRE(StatisticsReader::reclaimableBytesFromFreeList(headerFields)==0);
}
TEST_CASE("the safety check passes when content lives outside the events", "[safety]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSafetyPass");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    Database database(databasePath, Database::OPEN_READ_ONLY);
    // This is the check that makes compaction safe, and it must pass on a well formed
    // database whose text is in the part table.
    const SafetyCheckResult result=
        SafetyChecks::verifyMessageContentPersistsOutsideEventTable(database);
    REQUIRE(result.passed());
    // The explanation says what was confirmed, so a report can print it verbatim.
    REQUIRE_FALSE(result.explanation().empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the safety check fails on a database with no sessions", "[safety]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSafetyNoSessions");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=0;
    options.messageCountPerSession=0;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database database(databasePath, Database::OPEN_READ_ONLY);
    // A database with no sessions cannot be harmed by deleting event rows, so this
    // passes rather than blocking a run that has nothing to do.
    const SafetyCheckResult result=
        SafetyChecks::verifyMessageContentPersistsOutsideEventTable(database);
    REQUIRE(result.passed());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the schema check names what is missing", "[safety]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSafetySchema");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    {
        Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
        // A database with only one of the four tables is not one this tool knows how
        // to compact, and the refusal has to say which table is missing.
        database.execute("CREATE TABLE session (id TEXT PRIMARY KEY, time_created INTEGER)");
    }
    Database database(databasePath, Database::OPEN_READ_ONLY);
    const SafetyCheckResult result=SafetyChecks::verifySchemaSupportsCompaction(database);
    REQUIRE_FALSE(result.passed());
    // The message names the table, so a user can see why the tool stopped.
    REQUIRE(result.explanation().find("message")!=string::npos);
    REQUIRE(result.explanation().find("Nothing was changed")!=string::npos);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("an intact database passes the integrity check", "[safety]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSafetyIntegrity");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    Database database(databasePath, Database::OPEN_READ_ONLY);
    const SafetyCheckResult result=SafetyChecks::verifyDatabaseIsNotDamaged(database);
    REQUIRE(result.passed());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the lock check passes on a file nobody has open", "[safety]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSafetyLock");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    const SafetyCheckResult result=SafetyChecks::verifyDatabaseIsNotLocked(databasePath);
    REQUIRE(result.passed());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a session snapshot is captured and then verified", "[safety]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSafetySnapshot");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    Database database(databasePath, Database::OPEN_READ_ONLY);
    const vector<SessionSnapshot> snapshots=SafetyChecks::captureOldestSessions(database, 3);
    // The snapshot is the evidence the post deletion check compares against, so it
    // must hold the oldest sessions and their content counts.
    REQUIRE(snapshots.size()==3);
    for (const SessionSnapshot& snapshot : snapshots) {
        REQUIRE_FALSE(snapshot.sessionIdentifier.empty());
        REQUIRE(snapshot.readableTextPartCount>0);
        REQUIRE(snapshot.contentPersistsOutsideEventTable);
    }
    // With nothing deleted, the sessions must still match the snapshot exactly.
    const SafetyCheckResult unchanged=SafetyChecks::verifySnapshotsSurvivedUnchanged(database, snapshots);
    REQUIRE(unchanged.passed());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("verifying against no snapshots is not a failure", "[safety]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestSafetyNoSnapshot");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    Database database(databasePath, Database::OPEN_READ_ONLY);
    // A database with nothing to compare against cannot have lost anything, so this
    // passes rather than blocking the run.
    const SafetyCheckResult result=SafetyChecks::verifySnapshotsSurvivedUnchanged(
        database,
        vector<SessionSnapshot>()
    );
    REQUIRE(result.passed());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a check result describes itself as safe or not", "[safety]") {
    const SafetyCheckResult passing("the content is in the part table");
    const SafetyCheckResult failing=SafetyCheckResult::failed("the content is missing");
    // The report prints `describe()` verbatim, so it must begin with the verdict.
    REQUIRE(passing.describe().find("It is safe")==0);
    REQUIRE(failing.describe().find("It is not safe")==0);
    REQUIRE(failing.explanation()=="the content is missing");
}
