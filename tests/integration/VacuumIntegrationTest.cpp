// The integration test for the vacuum command.
//
// The vacuum is the operation that gives unused space back to the drive, and it is
// also the one that writes a second whole copy of the database. The tests check that
// it shrinks a bloated file, that it refuses when there is no room, that a preview
// writes nothing at all, and that it leaves the conversation exactly as it was.
#include <string>
// Provides `remove` and `exists`, which clear and check a test's own files.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the command under test.
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
// Provides the command that finds the safety copies.
#include "AgentDbDoctor/Command/BackupsCommand.hpp"
// Provides `ofstream`, which writes the interrupted swap journal in the test.
#include <fstream>
// Provides the options the command reads.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the database wrapper, so the test can read the file itself.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the platform metadata, for the sizes before and after.
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
// Counts the readable text across the whole database, which is what a conversation
// is. It is measured before and after so a vacuum that lost a session would be
// caught even though the file got smaller.
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
// Adds free pages to a database so it occupies more space than it needs, which is
// exactly the state a vacuum leaves behind.
void inflateWithFreePages(const string& databasePath, int freePageCount) {
    Database database(databasePath, Database::OPEN_READ_WRITE);
    // A table of filler pages that is then dropped leaves the pages on the free list
    // while the file stays the size it grew to. That is the state the vacuum exists to
    // undo, and it cannot be produced any other way.
    database.execute("PRAGMA auto_vacuum = NONE");
    database.execute("CREATE TABLE filler (id INTEGER PRIMARY KEY, body BLOB)");
    for (int pageIndex=0; pageIndex<freePageCount; ++pageIndex) {
        database.execute(
            "INSERT INTO filler (body) VALUES (zeroblob(3000))"
        );
    }
    database.execute("DROP TABLE filler");
    database.execute("PRAGMA wal_checkpoint(TRUNCATE)");
}
}
TEST_CASE("the vacuum gives unused space back and keeps the content", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumShrinks");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=10;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // Two thousand free pages is about eight megabytes of space the file is holding
    // but not using, which is the state a vacuum leaves behind.
    inflateWithFreePages(databasePath, 2000);
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.backupFilePath=databasePath+".test-backup";
    VacuumCommand command(false);
    const int exitCode=command.execute(commandOptions);
    REQUIRE(exitCode==0);
    REQUIRE(command.lastOutcome().wasCompacted);
    // The file must be smaller, which is the whole point of the command.
    REQUIRE(command.lastOutcome().sizeAfterBytes < sizeBefore);
    REQUIRE(command.lastOutcome().bytesReclaimed > 0);
    // And the content must be identical. A vacuum that lost a session would be a
    // catastrophic bug, and this is the check that would catch it.
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a vacuum of a file with no free pages still succeeds", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumNoFreeSpace");
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
    commandOptions.backupFilePath=databasePath+".test-backup";
    VacuumCommand command(false);
    command.execute(commandOptions);
    // Nothing is freed, and the tool has to say so rather than claiming a success
    // that produced no space.
    REQUIRE(command.lastOutcome().wasCompacted);
    REQUIRE(command.lastOutcome().bytesReclaimed==0);
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a vacuum preview changes nothing", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumPreview");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    inflateWithFreePages(databasePath, 500);
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.dryRunOnly=true;
    VacuumCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    const VacuumOutcome& outcome=command.lastOutcome();
    // A preview reports the numbers and writes nothing: the file is the same size,
    // no rewrite was started, and no safety copy was made.
    REQUIRE(outcome.wasPreviewOnly);
    REQUIRE_FALSE(outcome.wasCompacted);
    REQUIRE(outcome.bytesReclaimed>0);
    REQUIRE(outcome.summarySentence.find("Nothing was changed.")!=string::npos);
    REQUIRE(PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes==sizeBefore);
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".vacuuming"));
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".test-backup"));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the vacuum refuses a file that is not there", "[integration][vacuum]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=missingPath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    VacuumCommand command(false);
    // The command must not create the file it was asked to vacuum.
    REQUIRE(command.execute(commandOptions)==1);
    REQUIRE_FALSE(std::filesystem::exists(missingPath));
}
TEST_CASE("the vacuum works on a database no agent wrote", "[integration][vacuum]") {
    // Vacuuming needs no knowledge of any schema, only of the SQLite file format, so
    // it must work for every supported agent's database and for any other SQLite
    // file as well. This database has no session, message, or part table at all.
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumForeignSchema");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    {
        Database database(databasePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
        database.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT)");
        for (int rowIndex=0; rowIndex<400; ++rowIndex) {
            database.execute(
                "INSERT INTO notes (body) VALUES ('a note that is long enough to fill a page with text')"
            );
        }
        database.execute("DELETE FROM notes WHERE id % 2 = 1");
        database.execute("PRAGMA wal_checkpoint(TRUNCATE)");
    }
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.backupFilePath=databasePath+".test-backup";
    VacuumCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(command.lastOutcome().wasCompacted);
    REQUIRE(command.lastOutcome().sizeAfterBytes<sizeBefore);
    // And the rows that remained are untouched.
    Database reopenedDatabase(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE(reopenedDatabase.querySingleInteger("SELECT COUNT(*) FROM notes").value_or(0)==200);
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the vacuum leaves no half written file behind", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumNoLeftovers");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.backupFilePath=databasePath+".test-backup";
    VacuumCommand command(false);
    command.execute(commandOptions);
    // The file the rewrite is built into is renamed into place, so it must not
    // survive the run. A leftover would look like a database to the next tool that
    // looked in the folder.
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".vacuuming"));
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".before-vacuum"));
    // And the vacuumed file still opens.
    Database reopenedDatabase(databasePath, Database::OPEN_READ_ONLY);
    const auto integrityCheck=reopenedDatabase.querySingleText("PRAGMA quick_check");
    REQUIRE(integrityCheck.has_value());
    REQUIRE(integrityCheck.value()=="ok");
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the vacuum removes its safety copy once the file is in place", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumRemovesCopy");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=8;
    options.messageCountPerSession=4;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    inflateWithFreePages(databasePath, 400);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    VacuumCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(command.lastOutcome().wasCompacted);
    // The copy is gone and the space it held came back, which is what makes the
    // vacuum actually free the space it reports.
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).empty());
    REQUIRE(command.lastOutcome().keptSafetyCopyBytes==0);
    REQUIRE(command.lastOutcome().netBytesFreed==command.lastOutcome().bytesReclaimed);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the vacuum reports the net change when the copy is kept", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumNetChange");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=8;
    options.messageCountPerSession=4;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    inflateWithFreePages(databasePath, 400);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.keepBackup=true;
    VacuumCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(command.lastOutcome().wasCompacted);
    // Keeping the copy means the drive did not get everything back, and the net
    // number says so rather than letting the reclaimed figure stand alone.
    REQUIRE(command.lastOutcome().keptSafetyCopyBytes>0);
    REQUIRE(
        command.lastOutcome().netBytesFreed
        == command.lastOutcome().bytesReclaimed-command.lastOutcome().keptSafetyCopyBytes
    );
    const vector<BackupFile> backups=BackupsCommand::findBackupsBeside(databasePath);
    REQUIRE(backups.size()==1);
    for (const BackupFile& backup : backups) {
        std::filesystem::remove(backup.filePath);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("an interrupted swap is rolled back before the next run", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumInterruptedSwap");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=6;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    // The state a power loss between the first two renames leaves: the original has
    // been moved aside, the new file was never put in place, and the journal that
    // records the three names is there.
    const string displacedFilePath=databasePath+".before-vacuum";
    const string rewrittenFilePath=databasePath+".vacuuming";
    std::filesystem::rename(databasePath, displacedFilePath);
    {
        std::ofstream intentFile(databasePath+".vacuum-swap-intent", std::ios::trunc);
        intentFile << databasePath << "\n" << displacedFilePath << "\n" << rewrittenFilePath << "\n";
    }
    // A dry run is enough: resolving the interruption happens before everything else.
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.dryRunOnly=true;
    VacuumCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    // The original is back under its own name, with its content intact, and the
    // journal and the half finished swap are gone.
    REQUIRE(std::filesystem::exists(databasePath));
    REQUIRE_FALSE(std::filesystem::exists(displacedFilePath));
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".vacuum-swap-intent"));
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a swap interrupted after the new file was in place is cleaned up", "[integration][vacuum]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("VacuumInterruptedAfterSwap");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=6;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // The state a power loss after the new file was in place leaves: the database is
    // the new file, the old one is still beside it, and the journal is still there.
    const string displacedFilePath=databasePath+".before-vacuum";
    std::filesystem::copy_file(databasePath, displacedFilePath);
    {
        std::ofstream intentFile(databasePath+".vacuum-swap-intent", std::ios::trunc);
        intentFile << databasePath << "\n" << displacedFilePath << "\n" << databasePath+".vacuuming" << "\n";
    }
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.dryRunOnly=true;
    VacuumCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(std::filesystem::exists(databasePath));
    REQUIRE_FALSE(std::filesystem::exists(displacedFilePath));
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".vacuum-swap-intent"));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
