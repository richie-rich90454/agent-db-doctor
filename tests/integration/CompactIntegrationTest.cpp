// The integration test for the compact command.
//
// Compaction is the operation that makes the space usable again, and it is also the
// one that writes a second whole copy of the database. The tests check that it
// shrinks a bloated file, that it refuses when there is no room, and that it leaves
// the conversation exactly as it was.
#include <string>
// Provides `remove` and `exists`, which clear and check a test's own files.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the command under test.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
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
// is. It is measured before and after so a compaction that lost a session would be
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
// exactly the state a prune leaves behind.
void inflateWithFreePages(const string& databasePath, int freePageCount) {
    Database database(databasePath, Database::OPEN_READ_WRITE);
    // A table of filler pages that is then dropped leaves the pages on the free list
    // while the file stays the size it grew to. That is the state compaction exists to
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
TEST_CASE("compaction gives unused space back and keeps the content", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactShrinks");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=10;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // Two thousand free pages is about eight megabytes of space the file is holding
    // but not using, which is the state a prune leaves behind.
    inflateWithFreePages(databasePath, 2000);
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    const long long readablePartsBefore=countAllReadableTextParts(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    commandOptions.backupFilePath=databasePath+".test-backup";
    CompactCommand command(false);
    const int exitCode=command.execute(commandOptions);
    REQUIRE(exitCode==0);
    REQUIRE(command.lastOutcome().wasCompacted);
    // The file must be smaller, which is the whole point of the command.
    REQUIRE(command.lastOutcome().sizeAfterBytes < sizeBefore);
    REQUIRE(command.lastOutcome().bytesReclaimed > 0);
    // And the content must be identical. A compaction that lost a session would be a
    // catastrophic bug, and this is the check that would catch it.
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("compaction of a file with no free pages still succeeds", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactNoFreeSpace");
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
    CompactCommand command(false);
    command.execute(commandOptions);
    // Nothing is freed, and the tool has to say so rather than claiming a success
    // that produced no space.
    REQUIRE(command.lastOutcome().wasCompacted);
    REQUIRE(command.lastOutcome().bytesReclaimed==0);
    REQUIRE(countAllReadableTextParts(databasePath)==readablePartsBefore);
    std::filesystem::remove(commandOptions.backupFilePath);
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
    // The command must not create the file it was asked to compact.
    REQUIRE(command.execute(commandOptions)==1);
    REQUIRE_FALSE(std::filesystem::exists(missingPath));
}
TEST_CASE("compaction leaves no half written file behind", "[integration][compact]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompactNoLeftovers");
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
    CompactCommand command(false);
    command.execute(commandOptions);
    // The file the rewrite is built into is renamed into place, so it must not
    // survive the run. A leftover would look like a database to the next tool that
    // looked in the folder.
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".compacting"));
    REQUIRE_FALSE(std::filesystem::exists(databasePath+".before-compaction"));
    // And the compacted file still opens.
    Database reopenedDatabase(databasePath, Database::OPEN_READ_ONLY);
    const auto integrityCheck=reopenedDatabase.querySingleText("PRAGMA quick_check");
    REQUIRE(integrityCheck.has_value());
    REQUIRE(integrityCheck.value()=="ok");
    std::filesystem::remove(commandOptions.backupFilePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
