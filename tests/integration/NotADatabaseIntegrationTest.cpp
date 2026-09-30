// The integration test for inputs that are not databases.
//
// Several supported coding agents keep their history as plain text files rather than
// in a SQLite database, so every command has to be handed a folder or a text file at
// some point. These tests pin down what each command says when it is, because the
// wrong answers here are not crashes but misleading sentences: a folder described as a
// healthy database whose pages had all checked out, and a text file described as
// damaged, when neither statement was true.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the directory and file operations the tests need to make their own inputs.
#include <filesystem>
// Provides `ofstream`, which writes the plain text files the tests use as inputs.
#include <fstream>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the commands under test.
#include "AgentDbDoctor/Command/DiagnoseCommand.hpp"
#include "AgentDbDoctor/Command/RecoverCommand.hpp"
#include "AgentDbDoctor/Command/CompactCommand.hpp"
#include "AgentDbDoctor/Command/PruneCommand.hpp"
// Provides the options the commands read.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the diagnostic report, whose status is what the tests assert on.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
// Provides the platform metadata, so a test can prove a folder still exists.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder, for its temporary path helper.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Makes an empty folder and removes it again, so a test never leaves one behind.
struct TemporaryFolder final {
    explicit TemporaryFolder(const string& label)
        : path(SyntheticDatabaseBuilder::temporaryDatabasePath(label)) {
        std::error_code ignoredError;
        std::filesystem::remove_all(path, ignoredError);
        std::filesystem::create_directories(path, ignoredError);
    }
    ~TemporaryFolder() {
        std::error_code ignoredError;
        std::filesystem::remove_all(path, ignoredError);
    }
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
    string path;
};
// Makes a small text file that is not a database, and removes it again.
struct TemporaryTextFile final {
    explicit TemporaryTextFile(const string& label)
        : path(SyntheticDatabaseBuilder::temporaryDatabasePath(label)) {
        std::error_code ignoredError;
        std::filesystem::remove(path, ignoredError);
        std::ofstream written(path);
        written << "this is a conversation transcript, not a database";
    }
    ~TemporaryTextFile() {
        std::error_code ignoredError;
        std::filesystem::remove(path, ignoredError);
    }
    TemporaryTextFile(const TemporaryTextFile&) = delete;
    TemporaryTextFile& operator=(const TemporaryTextFile&) = delete;
    string path;
};
// A folder is a folder. The status has to say so, because every other status either
// means the file was inspected or that something is wrong with it, and neither is true.
void requireIsReportedAsAFolder(const string& path) {
    AgentDatabaseDoctor doctor;
    const DiagnosisReport report=doctor.diagnoseDatabase(path, string(), true);
    REQUIRE(report.getFileExists());
    REQUIRE(report.getIsDirectory());
    REQUIRE(report.getIntegrityStatus()==IntegrityStatus::IS_A_FOLDER);
    // A folder has no redundant snapshots to remove, whatever its size.
    REQUIRE_FALSE(report.getPruningSupported());
    // Nothing was lost and nothing is damaged, so the report must not claim otherwise.
    REQUIRE_FALSE(report.isHealthy());
}
}
TEST_CASE("a folder is reported as a folder rather than as a database", "[integration][notadatabase]") {
    const TemporaryFolder folder("NotADatabaseFolder");
    requireIsReportedAsAFolder(folder.path);
}
TEST_CASE("the diagnose command counts a folder apart from healthy and damaged", "[integration][notadatabase]") {
    const TemporaryFolder folder("NotADatabaseCount");
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=folder.path;
    commandOptions.quiet=true;
    DiagnoseCommand command(false);
    // A folder is neither a database that passed nor one that failed, so it is
    // counted on its own and the run exits zero: nothing needs a human.
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(command.problemDatabaseCount()==0);
    REQUIRE(command.healthyDatabaseCount()==0);
}
TEST_CASE("recover refuses a folder and explains that nothing is wrong", "[integration][notadatabase]") {
    const TemporaryFolder folder("NotADatabaseRecover");
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=folder.path;
    commandOptions.quiet=true;
    RecoverCommand command(false);
    // A refusal, so a non zero exit code. A folder is not a database that was repaired
    // and it is not a database that failed, so neither zero nor a damage verdict is
    // right: the command was asked about something it cannot act on.
    REQUIRE(command.execute(commandOptions)!=0);
}
TEST_CASE("recover does not build a working copy from a file that is not a database", "[integration][notadatabase]") {
    const TemporaryTextFile textFile("NotADatabaseRecoverText");
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=textFile.path;
    commandOptions.quiet=true;
    commandOptions.rebuildWorkingCopy=true;
    commandOptions.answerYesToEverything=true;
    RecoverCommand command(false);
    // The diagnosis is still printed, because a file that cannot be read as a database
    // is exactly the case this command exists to explain.
    REQUIRE(command.execute(commandOptions)==0);
    const RecoverOutcome outcome=command.lastOutcome();
    REQUIRE(outcome.dataIsUnrecoverable);
    // Nothing was created beside the file. Rebuilding one used to produce an empty
    // database and report it as a success with nothing in it.
    REQUIRE_FALSE(std::filesystem::exists(textFile.path + ".rebuilt"));
    REQUIRE(outcome.rebuiltDatabasePath.empty());
}
TEST_CASE("compact refuses a folder without copying it", "[integration][notadatabase]") {
    const TemporaryFolder folder("NotADatabaseCompactFolder");
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=folder.path;
    commandOptions.quiet=true;
    commandOptions.answerYesToEverything=true;
    CompactCommand command(false);
    // A refusal, so a non zero exit code: the command was asked to do something it
    // cannot and should not quietly report success.
    REQUIRE(command.execute(commandOptions)!=0);
    // Nothing was written, which is the promise that matters.
    const FileMetadata folderStatus=PlatformMetadata::readFileMetadata(folder.path);
    REQUIRE(folderStatus.exists);
    REQUIRE(folderStatus.isDirectory);
}
TEST_CASE("prune refuses a folder without opening it", "[integration][notadatabase]") {
    const TemporaryFolder folder("NotADatabasePruneFolder");
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=folder.path;
    commandOptions.quiet=true;
    commandOptions.answerYesToEverything=true;
    PruneCommand command(false);
    command.execute(commandOptions);
    const PruneOutcome outcome=command.lastOutcome();
    REQUIRE_FALSE(outcome.anythingWasRemoved);
    REQUIRE(outcome.removedRowCount==0);
}
TEST_CASE("a text file is refused by compact in plain language", "[integration][notadatabase]") {
    const TemporaryTextFile textFile("NotADatabaseCompactText");
    const FileMetadata before=PlatformMetadata::readFileMetadata(textFile.path);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=textFile.path;
    commandOptions.quiet=true;
    commandOptions.answerYesToEverything=true;
    CompactCommand command(false);
    REQUIRE(command.execute(commandOptions)!=0);
    // The refusal happens before any work, so the file is untouched. Compacting a text
    // file used to copy it, then rewrite it, then report that the database was damaged.
    const FileMetadata after=PlatformMetadata::readFileMetadata(textFile.path);
    REQUIRE(after.logicalSizeBytes==before.logicalSizeBytes);
}
TEST_CASE("a text file is refused by prune in plain language", "[integration][notadatabase]") {
    const TemporaryTextFile textFile("NotADatabasePruneText");
    const FileMetadata before=PlatformMetadata::readFileMetadata(textFile.path);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=textFile.path;
    commandOptions.quiet=true;
    commandOptions.answerYesToEverything=true;
    PruneCommand command(false);
    command.execute(commandOptions);
    REQUIRE(command.lastOutcome().removedRowCount==0);
    const FileMetadata after=PlatformMetadata::readFileMetadata(textFile.path);
    REQUIRE(after.logicalSizeBytes==before.logicalSizeBytes);
}
TEST_CASE("an empty file says it is empty rather than that its first bytes are wrong", "[integration][notadatabase]") {
    const string emptyPath=SyntheticDatabaseBuilder::temporaryDatabasePath("NotADatabaseEmpty");
    SyntheticDatabaseBuilder::removeIfPresent(emptyPath);
    {
        std::ofstream created(emptyPath);
    }
    AgentDatabaseDoctor doctor;
    const DiagnosisReport report=doctor.diagnoseDatabase(emptyPath, string(), true);
    // The file exists and is not a database. Saying its first bytes are not the SQLite
    // signature would describe bytes that are not there.
    REQUIRE(report.getFileExists());
    REQUIRE(report.getIntegrityStatus()==IntegrityStatus::NOT_A_DATABASE);
    REQUIRE(report.getLogicalSizeBytes()==0);
    SyntheticDatabaseBuilder::removeIfPresent(emptyPath);
}