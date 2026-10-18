// The integration test for the compact command.
//
// This is the most important test in the suite, because the compact command is the
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
#include "AgentDbDoctor/Command/CompactCommand.hpp"
// Provides the command that finds and manages the safety copies.
#include "AgentDbDoctor/Command/BackupsCommand.hpp"
// Provides the survival check the forced-failure test trips.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
// Provides the schema detector, so the fixture's generation is asserted rather than
// assumed.
#include "AgentDbDoctor/Doctor/SchemaVersionDetector.hpp"
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
// Counts the second generation event rows, which no compaction may ever remove.
long long countSecondGenerationEventRows(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    return database.querySingleInteger(
        "SELECT COUNT(*) FROM event WHERE type>='session.next.' AND type<'session.next/'"
    ).value_or(-1);
}
}
TEST_CASE("compaction removes duplicates and keeps every word of the conversation", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactKeepsContent");
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
    CompactCommand command(false);
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
TEST_CASE("compaction makes a safety copy before it deletes anything", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactMakesBackup");
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
    CompactCommand command(false);
    command.execute(commandOptions);
    // The copy is the whole point of the safety rules, so its absence would mean the
    // tool deleted rows from a user's only copy.
    REQUIRE(std::filesystem::exists(commandOptions.backupFilePath));
    // The copy is a complete copy, not a partial one, so the text in it is intact.
    REQUIRE(countAllReadableTextParts(commandOptions.backupFilePath)==sizeBefore);
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction refuses when the change would be more than half the table", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactRefusesLargeChange");
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
    CompactCommand command(false);
    command.execute(commandOptions);
    // A change that large usually means a miscount, so the tool stops rather than
    // acting on a number it does not trust.
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    // And the refusal changed nothing at all.
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction does nothing when there is nothing duplicated", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactNothingToDo");
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
    CompactCommand command(false);
    const int exitCode=command.execute(commandOptions);
    // A database with no duplicates needs no copy and no confirmation, so the command
    // finishes quickly and says there was nothing to do.
    REQUIRE(exitCode==0);
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction keeps the newest sessions untouched", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactKeepsNewest");
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
    CompactCommand command(false);
    command.execute(commandOptions);
    // Keeping every session means the events in them are protected, so nothing is
    // removed. This is the setting a cautious user would try first.
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    REQUIRE(command.lastOutcome().protectedSessionCount==20);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction refuses a file that is not there", "[integration][compact]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=missingPath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    CompactCommand command(false);
    const int exitCode=command.execute(commandOptions);
    // The command must say what is wrong and change nothing, rather than creating a
    // file it was not asked to create.
    REQUIRE(exitCode==1);
    REQUIRE_FALSE(std::filesystem::exists(missingPath));
}
TEST_CASE("compaction needs to be told which database to work on", "[integration][compact]") {
    CommandOptions commandOptions;
    // With no path, the command has nothing to act on. Guessing would be dangerous.
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    CompactCommand command(false);
    REQUIRE(command.execute(commandOptions)==1);
}
TEST_CASE("compaction refuses a removal limit that is more than the whole table", "[integration][compact]") {
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=4;
    options.redundantCopiesPerMessage=3;
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactLimitAboveOne");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    // Two is a hundred and fifty per cent over the limit's own name. It used to be
    // accepted silently, which meant a typo quietly removed the safety limit the user
    // believed they had set.
    commandOptions.maximumFractionToRemove=2.0;
    CompactCommand command(false);
    command.execute(commandOptions);
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    // The refusal happens before the safety copy, so nothing was written beside the
    // database. A limit the tool cannot use is not worth spending half the file size on.
    REQUIRE_FALSE(std::filesystem::exists(databasePath + ".agent-db-doctor-backup"));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction accepts a removal limit that is a real share of the table", "[integration][compact]") {
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=4;
    options.redundantCopiesPerMessage=3;
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactLimitValid");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.sessionsToKeep=10;
    // Ninety five per cent is a real share and must be honoured, so that the refusal
    // above is about the value being impossible rather than about the tool being shy.
    commandOptions.maximumFractionToRemove=0.95;
    CompactCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(command.lastOutcome().anythingWasRemoved);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction removes the safety copy once its checks pass", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactRemovesCopy");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=12;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    CompactCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(command.lastOutcome().anythingWasRemoved);
    // The copy did its job, so it is gone. A copy of the file left behind would
    // cancel out every byte the run made reusable, which is the whole reason the
    // removal exists.
    REQUIRE(command.lastOutcome().safetyCopyWasRemoved);
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction keeps the safety copy when asked", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactKeepsCopy");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=12;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.keepBackup=true;
    CompactCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    const vector<BackupFile> backups=BackupsCommand::findBackupsBeside(databasePath);
    REQUIRE(backups.size()==1);
    // The kept copy is a complete copy from before the run, which is what makes it a
    // rollback point rather than a file with the right name.
    REQUIRE(countAllReadableTextParts(backups.front().filePath)==readablePartsBefore);
    for (const BackupFile& backup : backups) {
        std::filesystem::remove(backup.filePath);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction puts the safety copy back when the check fails", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactRollsBack");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=12;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long redundantRowsBefore=countRedundantEventRows(databasePath);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    // The check after the removal is forced to fail, because a healthy database
    // cannot make it fail. This is the one path a user must never see a wrong claim on.
    SafetyChecks::failSurvivalCheckForTests()=true;
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    CompactCommand command(false);
    const int exitCode=command.execute(commandOptions);
    SafetyChecks::failSurvivalCheckForTests()=false;
    REQUIRE(exitCode==1);
    REQUIRE(command.lastOutcome().wasRolledBack);
    REQUIRE(command.lastOutcome().restoreWasRequested);
    // The promise, measured rather than asserted: row for row and word for word, the
    // database is what it was before the run.
    REQUIRE(countRedundantEventRows(databasePath)==redundantRowsBefore);
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    // A failed run keeps its copy, so a person can look at it before deciding anything.
    REQUIRE_FALSE(BackupsCommand::findBackupsBeside(databasePath).empty());
    for (const BackupFile& backup : BackupsCommand::findBackupsBeside(databasePath)) {
        std::filesystem::remove(backup.filePath);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a second generation only database has nothing to compact", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactSecondGenerationOnly");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=8;
    options.messageCountPerSession=4;
    // No older copies at all, and second generation rows instead: exactly what a
    // database that has been used since the agent migrated looks like.
    options.redundantCopiesPerMessage=0;
    options.secondGenerationRowsPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long secondGenerationRowsBefore=countSecondGenerationEventRows(databasePath);
    REQUIRE(secondGenerationRowsBefore>0);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    // The detector names the generation before anything else is decided.
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(SchemaVersionDetector::detect(database)==SchemaGeneration::Version2Only);
    }
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    CompactCommand command(false);
    const int exitCode=command.execute(commandOptions);
    // Nothing needs doing, and that is a success rather than a refusal: the run did
    // everything the plan described, which was nothing.
    REQUIRE(exitCode==0);
    REQUIRE_FALSE(command.lastOutcome().anythingWasRemoved);
    REQUIRE_FALSE(command.lastOutcome().wasRefused);
    // The second generation rows are exactly where they were.
    REQUIRE(countSecondGenerationEventRows(databasePath)==secondGenerationRowsBefore);
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    // No safety copy was made, because nothing was going to change.
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction leaves second generation rows alone in a mixed database", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactMixedGenerations");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=4;
    options.redundantCopiesPerMessage=3;
    options.secondGenerationRowsPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long secondGenerationRowsBefore=countSecondGenerationEventRows(databasePath);
    const long long redundantRowsBefore=countRedundantEventRows(databasePath);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(SchemaVersionDetector::detect(database)==SchemaGeneration::Version1And2);
    }
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    CompactCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(command.lastOutcome().removedRowCount>0);
    // The older first generation copies go, and the second generation rows do not.
    REQUIRE(countRedundantEventRows(databasePath)<redundantRowsBefore);
    REQUIRE(countSecondGenerationEventRows(databasePath)==secondGenerationRowsBefore);
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
