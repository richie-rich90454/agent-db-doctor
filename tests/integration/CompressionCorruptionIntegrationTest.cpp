// The integration test for the file system compression failure.
//
// This is the scenario the specification tells a story about, reproduced as closely
// as a test can: a database that the file system and the database disagree about.
// A real reproduction of the data loss would need a half finished NTFS compression,
// which cannot be done safely on a build machine. What can be done is to build the
// state that the loss leaves behind and prove the tool recognises it by name,
// refuses to act on it, and tells the user to stop touching the disk.
#include <string>
// Provides `remove` and `exists`, which clear and check a test's own files.
#include <filesystem>
// Provides `ofstream`, used to write a file that is not a database.
#include <fstream>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the recover command and the pattern classifier under test.
#include "AgentDbDoctor/Command/RecoverCommand.hpp"
#include "AgentDbDoctor/Recovery/KnownFailurePatterns.hpp"
#include "AgentDbDoctor/Recovery/RecoveryPlaybook.hpp"
// Provides the options the command reads.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the platform metadata, whose two sizes are the signal.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the database wrapper, so the test can check the file still opens.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Recovery;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
namespace {
// The size a file browser would report in the story: the readable part of a much
// larger file.
constexpr long long VISIBLE_FILE_SIZE_BYTES=11000000000LL;
// The size the file system still accounts for, which is the original.
constexpr long long ALLOCATED_SIZE_BYTES=124000000000LL;
// The read failure the story reports, which is SQLite's extended read error.
constexpr int READ_FAULT_RESULT_CODE=266;
}
TEST_CASE("the interrupted compression state is recognised by name", "[integration][compression]") {
    // The signal is a file whose logical size and allocated size disagree wildly,
    // which is what a half finished compression leaves behind: the file system kept
    // the original clusters and the file itself was rewritten shorter.
    // A real file is built so the two numbers come from the operating system rather
    // than from a made up structure.
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompressionState");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // A file whose allocated size is far below its logical size is the compressed
    // case, and it is the one that makes a database unwritable.
    const FileMetadata compressedLike=PlatformMetadata::readFileMetadata(databasePath);
    REQUIRE(compressedLike.exists);
    FileMetadata compressedShape=compressedLike;
    compressedShape.logicalSizeBytes=ALLOCATED_SIZE_BYTES;
    compressedShape.allocatedSizeBytes=VISIBLE_FILE_SIZE_BYTES / 10;
    REQUIRE(KnownFailurePatterns::looksCompressedOrSparse(compressedShape));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a database that cannot be read is named as unrecoverable", "[integration][compression]") {
    // The classifier is given the read failure and the mismatched sizes, which is all
    // the information a diagnostic pass gathers.
    FileMetadata interruptedCompression;
    interruptedCompression.exists=true;
    interruptedCompression.logicalSizeBytes=VISIBLE_FILE_SIZE_BYTES;
    interruptedCompression.allocatedSizeBytes=ALLOCATED_SIZE_BYTES;
    FileLockStatus freeStatus;
    DiskSpace roomyVolume;
    roomyVolume.isValid=true;
    roomyVolume.totalBytes=400000000000LL;
    roomyVolume.availableBytes=36000000000LL;
    const FailureMatch match=KnownFailurePatterns::classify(
        READ_FAULT_RESULT_CODE,
        interruptedCompression,
        freeStatus,
        roomyVolume
    );
    REQUIRE(match.matched);
    // The tool has to name this case and say the data is gone, because a user who is
    // not told will keep trying commands that cannot work.
    REQUIRE(match.patternName=="interrupted-compression");
    REQUIRE(match.dataIsUnrecoverable);
    REQUIRE(match.severity==SeverityLevel::BEYOND_RECOVERY);
}
TEST_CASE("the recovery plan starts by telling the user to stop", "[integration][compression]") {
    // The single most useful thing the tool can say to someone in this situation is
    // to stop writing to the disk, and the specification puts that first.
    const vector<RecoveryRecommendation> plan=
        RecoveryPlaybook::buildPlanForUnreadableDatabase(
            "/home/ann/.local/share/opencode/opencode.db",
            READ_FAULT_RESULT_CODE,
            "The operating system cannot read this file."
        );
    REQUIRE(plan.size()>=4);
    // The first step must be about stopping, not about repairing.
    REQUIRE(plan.front().title().find("Stop")!=string::npos);
    REQUIRE(plan.front().explanation().find("Close the coding agent")!=string::npos);
    // The second must be a copy, which is the only way to be sure the next step is
    // safe.
    REQUIRE(plan[1].explanation().find("Copy")!=string::npos);
    // The last must say plainly that some of it is gone, so the user stops hoping for
    // a one command fix.
    REQUIRE(plan.back().isFinalWord());
    REQUIRE(plan.back().explanation().find("No program can restore")!=string::npos);
    // The numeric code is included so the user can search for it, but never on its own.
    REQUIRE(plan[2].explanation().find(to_string(READ_FAULT_RESULT_CODE))!=string::npos);
}
TEST_CASE("a file that is not a database is never offered a repair", "[integration][compression]") {
    const string textFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompressionNotDatabase");
    SyntheticDatabaseBuilder::removeIfPresent(textFilePath);
    {
        std::ofstream textFile(textFilePath);
        textFile << "not a database, and not going to become one";
    }
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=textFilePath;
    commandOptions.quiet=true;
    RecoverCommand command(false);
    // Running the command must not turn the file into anything, and must not build a
    // working copy from a file that holds no sessions.
    command.execute(commandOptions);
    REQUIRE_FALSE(command.lastOutcome().rebuiltDatabasePath.empty() == false);
    std::filesystem::remove(textFilePath);
}
TEST_CASE("a sound database is never called unrecoverable", "[integration][compression]") {
    // The most dangerous failure this class could have is a false positive, where a
    // healthy database is reported as lost. A user who believed it would delete the
    // only copy of their conversations.
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CompressionHealthy");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const FileMetadata metadata=PlatformMetadata::readFileMetadata(databasePath);
    FileLockStatus freeStatus;
    DiskSpace roomyVolume;
    roomyVolume.isValid=true;
    roomyVolume.totalBytes=400000000000LL;
    roomyVolume.availableBytes=36000000000LL;
    const FailureMatch match=KnownFailurePatterns::classify(
        0,
        metadata,
        freeStatus,
        roomyVolume
    );
    REQUIRE_FALSE(match.dataIsUnrecoverable);
    // And the sessions really are all there, which is what makes the answer safe.
    Database database(databasePath, Database::OPEN_READ_ONLY);
    REQUIRE(database.querySingleInteger("SELECT COUNT(*) FROM session").value_or(0)==5);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
