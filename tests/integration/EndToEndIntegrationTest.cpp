// The integration test for the whole flow, from diagnosis to compaction.
//
// The commands are safe individually and the tests prove that. This file proves
// something they cannot: that running them in the order a person would run them
// produces the result the tool promises, and that the conversation survives the
// whole sequence. A safety rule that only holds in one command is not a safety rule.
#include <string>
// Provides `remove` and `exists`, which clear and check a test's own files.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the three commands in the order a user would run them.
#include "AgentDbDoctor/Command/DiagnoseCommand.hpp"
#include "AgentDbDoctor/Command/PruneCommand.hpp"
#include "AgentDbDoctor/Command/CompactCommand.hpp"
// Provides the shared options type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the database wrapper, so the test can check the result itself.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the platform metadata, for the size before and after.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Reads the whole conversation out of the database, so the before and after can be
// compared exactly. This is the measure that matters: not that the file got smaller,
// but that not one word of it was lost.
long long readEveryMessageBody(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    Statement readingStatement(
        database.rawHandle(),
        "SELECT data FROM part WHERE json_extract(data, '$.type')='text' ORDER BY id",
        "a read of every message body"
    );
    if (!readingStatement.isValid()) {
        return 0;
    }
    long long totalLength=0;
    long long rowCount=0;
    while (readingStatement.step()) {
        totalLength+=static_cast<long long>(readingStatement.columnTextView(0).size());
        rowCount++;
    }
    // Both the count and the total length are returned together, so a loss of one
    // character is caught as well as a loss of a whole row.
    return totalLength + rowCount * 1000000;
}
long long readSessionCount(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    return database.querySingleInteger("SELECT COUNT(*) FROM session").value_or(0);
}
}
TEST_CASE("diagnose then prune then compact leaves the conversation intact", "[integration][endtoend]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("EndToEndFlow");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=10;
    options.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // The whole conversation is measured before anything runs.
    const long long contentBefore=readEveryMessageBody(databasePath);
    const long long sessionsBefore=readSessionCount(databasePath);
    REQUIRE(contentBefore>0);
    // Free pages are added so there is something for the compaction to give back,
    // which is the state a real bloated database is in.
    {
        Database inflaterDatabase(databasePath, Database::OPEN_READ_WRITE);
        inflaterDatabase.execute("CREATE TABLE filler (id INTEGER PRIMARY KEY, body BLOB)");
        for (int pageIndex=0; pageIndex<1500; ++pageIndex) {
            inflaterDatabase.execute("INSERT INTO filler (body) VALUES (zeroblob(3000))");
        }
        inflaterDatabase.execute("DROP TABLE filler");
        inflaterDatabase.execute("PRAGMA wal_checkpoint(TRUNCATE)");
    }
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    // The answers are given in advance, which is what a scheduled run does and what
    // makes the flow reproducible.
    CommandOptions diagnoseOptions;
    diagnoseOptions.databaseFilePath=databasePath;
    diagnoseOptions.quiet=true;
    DiagnoseCommand diagnoseCommand(false);
    REQUIRE(diagnoseCommand.execute(diagnoseOptions)==0);
    CommandOptions pruneOptions;
    pruneOptions.databaseFilePath=databasePath;
    pruneOptions.answerYesToEverything=true;
    pruneOptions.quiet=true;
    pruneOptions.backupFilePath=databasePath+".flow-backup";
    PruneCommand pruneCommand(false);
    REQUIRE(pruneCommand.execute(pruneOptions)==0);
    REQUIRE(pruneCommand.lastOutcome().anythingWasRemoved);
    CommandOptions compactOptions;
    compactOptions.databaseFilePath=databasePath;
    compactOptions.answerYesToEverything=true;
    compactOptions.quiet=true;
    compactOptions.backupFilePath=databasePath+".flow-backup-compact";
    CompactCommand compactCommand(false);
    REQUIRE(compactCommand.execute(compactOptions)==0);
    REQUIRE(compactCommand.lastOutcome().wasCompacted);
    // Every session survived both operations.
    REQUIRE(readSessionCount(databasePath)==sessionsBefore);
    // And not one character of the conversation was lost by either one.
    REQUIRE(readEveryMessageBody(databasePath)==contentBefore);
    // The file really did get smaller, which is the whole reason to run the sequence.
    const long long sizeAfter=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    REQUIRE(sizeAfter<sizeBefore);
    // The file the agent will open next is a sound one.
    Database finalDatabase(databasePath, Database::OPEN_READ_ONLY);
    const auto integrityCheck=finalDatabase.querySingleText("PRAGMA quick_check");
    REQUIRE(integrityCheck.has_value());
    REQUIRE(integrityCheck.value()=="ok");
    std::filesystem::remove(pruneOptions.backupFilePath);
    std::filesystem::remove(compactOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("running the flow twice in a row is safe", "[integration][endtoend]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("EndToEndTwice");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long contentBefore=readEveryMessageBody(databasePath);
    CommandOptions pruneOptions;
    pruneOptions.databaseFilePath=databasePath;
    pruneOptions.answerYesToEverything=true;
    pruneOptions.quiet=true;
    pruneOptions.backupFilePath=databasePath+".twice-backup";
    PruneCommand firstRun(false);
    firstRun.execute(pruneOptions);
    PruneCommand secondRun(false);
    secondRun.execute(pruneOptions);
    // The second run has nothing left to remove, and a tool that kept going anyway
    // would be a tool doing pointless work on a large file.
    REQUIRE_FALSE(secondRun.lastOutcome().anythingWasRemoved);
    // Running a command twice must never cost content, which is the property a user
    // needs when they are not sure whether it already ran.
    REQUIRE(readEveryMessageBody(databasePath)==contentBefore);
    std::filesystem::remove(pruneOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a refused prune leaves the database ready for the next run", "[integration][endtoend]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("EndToEndRefused");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long contentBefore=readEveryMessageBody(databasePath);
    CommandOptions pruneOptions;
    pruneOptions.databaseFilePath=databasePath;
    // A limit of zero is impossible, so the command has to refuse before it starts.
    pruneOptions.maximumFractionToRemove=0.0;
    pruneOptions.answerYesToEverything=true;
    pruneOptions.quiet=true;
    PruneCommand command(false);
    command.execute(pruneOptions);
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    REQUIRE(readEveryMessageBody(databasePath)==contentBefore);
    // No copy was made either, because nothing was attempted.
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".backup"));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
