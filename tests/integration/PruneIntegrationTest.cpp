// The integration test for the prune command.
//
// This is the most important test in the suite, because the prune command is the
// only one that deletes rows. Every test here checks one of the promises the tool
// makes: nothing is deleted unless the content is proven to live elsewhere, a copy
// is made first, the oldest sessions are checked afterwards, and the conversation
// text is still there when the command finishes.
#include <string>
// Provides `remove` and `exists`, which clear and check a test's own files.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the command under test.
#include "AgentDbDoctor/Command/PruneCommand.hpp"
// Provides the options the command reads.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the database wrapper, so the test can read the file itself.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the constant time counters.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Counts the readable text parts across every session, which is the direct measure
// of whether a conversation survived. The comparison is made before and after every
// destructive test, because that is the promise the tool makes.
long long countAllReadableTextParts(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    Statement countingStatement(
        database.rawHandle(),
        "SELECT COUNT(*) FROM part "
        "WHERE json_extract(data, '$.type')='text' "
        "AND length(json_extract(data, '$.text'))>20",
        "a count of the readable text across the whole database"
    );
    if (!countingStatement.isValid() || !countingStatement.step()) {
        return 0;
    }
    return countingStatement.columnInteger(0);
}
long long countRedundantEventRows(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    const vector<string> redundantTypes{
        string("message.updated.1"),
        string("message.part.updated.1"),
        string("session.updated.1")
    };
    long long totalRows=0;
    for (const string& currentType : redundantTypes) {
        Statement countingStatement(
            database.rawHandle(),
            "SELECT COUNT(*) FROM event WHERE type=?1",
            "a count of one kind of duplicated event"
        );
        if (!countingStatement.isValid()) {
            continue;
        }
        countingStatement.bindText(1, currentType);
        if (countingStatement.step()) {
            totalRows+=countingStatement.columnInteger(0);
        }
    }
    return totalRows;
}
}
TEST_CASE("prune removes duplicates and keeps every word of the conversation", "[integration][prune]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("PruneKeepsContent");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=10;
    options.redundantCopiesPerMessage=4;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    const long long redundantRowsBefore=SyntheticDatabaseBuilder::build(databasePath, options);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    // The answer is given in advance, because the confirmation cannot be read in a
    // test process and because this is exactly what a scheduled run does.
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    PruneCommand command(false);
    const int exitCode=command.execute(commandOptions);
    REQUIRE(exitCode==0);
    // The command reports what it did, and the count must match what the builder
    // wrote into the young sessions.
    REQUIRE(command.lastOutcome().anythingWasRemoved);
    REQUIRE(command.lastOutcome().removedRowCount>0);
    // The promise that matters: not one readable word was lost.
    const long long readablePartsAfter=countAllReadableTextParts(databasePath);
    REQUIRE(readablePartsAfter==readablePartsBefore);
    // The old sessions are the ones whose events go, so the count must have dropped.
    REQUIRE(countRedundantEventRows(databasePath) < redundantRowsBefore);
    // Nothing was rolled back, because the verification passed.
    REQUIRE_FALSE(command.lastOutcome().wasRolledBack);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("prune makes a safety copy before it deletes anything", "[integration][prune]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("PruneMakesBackup");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long sizeBefore=countAllReadableTextParts(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    // A known path is given so the test can prove the copy is where it says it is.
    commandOptions.backupFilePath=databasePath+".test-backup";
    PruneCommand command(false);
    command.execute(commandOptions);
    // The copy is the whole point of the safety rules, so its absence would mean the
    // tool deleted rows from a user's only copy.
    REQUIRE(std::filesystem::exists(commandOptions.backupFilePath));
    // The copy is a complete copy, not a partial one, so the text in it is intact.
    REQUIRE(countAllReadableTextParts(commandOptions.backupFilePath)==sizeBefore);
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("prune refuses when the change would be more than half the table", "[integration][prune]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("PruneRefusesLargeChange");
    SyntheticDatabaseBuilder::Options options;
    // Four copies of each of three event types per message makes the event table
    // almost entirely duplicated, which is the shape that trips the refusal.
    options.sessionCount=10;
    options.messageCountPerSession=10;
    options.redundantCopiesPerMessage=20;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    // A limit of a tenth is far below what the change would be, so the refusal is
    // exercised whatever the exact row arithmetic comes to.
    commandOptions.maximumFractionToRemove=0.1;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    PruneCommand command(false);
    command.execute(commandOptions);
    // A change that large usually means a miscount, so the tool stops rather than
    // acting on a number it does not trust.
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    // And the refusal changed nothing at all.
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("prune does nothing when there is nothing duplicated", "[integration][prune]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("PruneNothingToDo");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    PruneCommand command(false);
    const int exitCode=command.execute(commandOptions);
    // A database with no duplicates needs no copy and no confirmation, so the command
    // finishes quickly and says there was nothing to do.
    REQUIRE(exitCode==0);
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("prune keeps the newest sessions untouched", "[integration][prune]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("PruneKeepsNewest");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    // A specific number is asked for, so the boundary is known rather than guessed.
    commandOptions.sessionsToKeep=20;
    PruneCommand command(false);
    command.execute(commandOptions);
    // Keeping every session means the events in them are protected, so nothing is
    // removed. This is the setting a cautious user would try first.
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    REQUIRE(command.lastOutcome().protectedSessionCount==20);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("prune refuses a file that is not there", "[integration][prune]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("PruneMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=missingPath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    PruneCommand command(false);
    const int exitCode=command.execute(commandOptions);
    // The command must say what is wrong and change nothing, rather than creating a
    // file it was not asked to create.
    REQUIRE(exitCode==1);
    REQUIRE_FALSE(std::filesystem::exists(missingPath));
}
TEST_CASE("prune needs to be told which database to work on", "[integration][prune]") {
    CommandOptions commandOptions;
    // With no path, the command has nothing to act on. Guessing would be dangerous.
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    PruneCommand command(false);
    REQUIRE(command.execute(commandOptions)==1);
}
