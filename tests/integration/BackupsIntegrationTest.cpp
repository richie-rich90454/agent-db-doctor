// The integration test for the backups command.
//
// The command is the other half of the safety promise: a copy nobody can find,
// test, or clear away is not a safety net. These tests check that the copies the
// guard writes are discoverable, that a drill proves one restorable, that restoring
// never deletes the file it replaces, and that deleting refuses to guess.
#include <string>
// Provides `remove`, `exists`, and `directory_iterator`, which the test uses to
// check what is left beside the database.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the command under test.
#include "AgentDbDoctor/Command/BackupsCommand.hpp"
// Provides the compact command, which is what writes a real safety copy.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
// Provides the options the commands read.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the database wrapper, so the test can read and change the file itself.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides StatisticsReader, for the table check after a restore.
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
// Counts the readable text across the whole database, the direct measure of whether
// a conversation survived.
long long countReadableTextParts(const string& databasePath) {
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
// Builds a small database and compacts it with `--keep-backup`, which leaves
// exactly one real safety copy for the command under test to work on.
void buildDatabaseWithOneSafetyCopy(const string& databasePath) {
    SyntheticDatabaseBuilder::Options builderOptions;
    builderOptions.sessionCount=10;
    builderOptions.messageCountPerSession=4;
    builderOptions.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, builderOptions);
    CommandOptions compactOptions;
    compactOptions.databaseFilePath=databasePath;
    compactOptions.answerYesToEverything=true;
    compactOptions.quiet=true;
    compactOptions.keepBackup=true;
    CompactCommand compactCommand(false);
    compactCommand.execute(compactOptions);
}
// Removes everything the tests or the tool left beside the database.
void removeEverythingBeside(const string& databasePath) {
    for (const BackupFile& backup : BackupsCommand::findBackupsBeside(databasePath)) {
        std::filesystem::remove(backup.filePath);
    }
    const std::filesystem::path folderPath=std::filesystem::path(databasePath).parent_path();
    const string databaseFileName=std::filesystem::path(databasePath).filename().string();
    std::error_code directoryError;
    std::filesystem::directory_iterator entryIterator(folderPath, directoryError);
    if (directoryError) {
        return;
    }
    for (const std::filesystem::directory_entry& entry : entryIterator) {
        const string fileName=entry.path().filename().string();
        if (fileName.rfind(databaseFileName + ".before-restore-", 0)==0
            || fileName.rfind(databaseFileName + ".before-vacuum", 0)==0
            || fileName.rfind(databaseFileName + ".vacuuming", 0)==0
            || fileName.rfind(databaseFileName + ".vacuum-swap-intent", 0)==0) {
            std::error_code removalError;
            std::filesystem::remove(entry.path(), removalError);
        }
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
}
TEST_CASE("the backups command lists the copy the guard wrote", "[integration][backups]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BackupsList");
    buildDatabaseWithOneSafetyCopy(databasePath);
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).size()==1);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    commandOptions.backupsAction="list";
    BackupsCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    const vector<BackupFile> backups=BackupsCommand::findBackupsBeside(databasePath);
    REQUIRE(backups.size()==1);
    // The size reported is the size on disk, so it is never zero for a real copy.
    REQUIRE(backups.front().sizeBytes>0);
    removeEverythingBeside(databasePath);
}
TEST_CASE("the restore drill proves a copy can be put back", "[integration][backups]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BackupsVerify");
    buildDatabaseWithOneSafetyCopy(databasePath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    commandOptions.backupsAction="verify";
    BackupsCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    // The drill works on a scratch copy, so neither the database nor the safety copy
    // is touched, and nothing is left over.
    REQUIRE(std::filesystem::exists(databasePath));
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).size()==1);
    REQUIRE_FALSE(std::filesystem::exists(
        BackupsCommand::findBackupsBeside(databasePath).front().filePath + ".verify-restore"
    ));
    removeEverythingBeside(databasePath);
}
TEST_CASE("a restore puts the copy back and moves the current file aside", "[integration][backups]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BackupsRestore");
    buildDatabaseWithOneSafetyCopy(databasePath);
    const long long readablePartsBeforeRestore=countReadableTextParts(databasePath);
    // A table added after the copy was made marks the file that a restore must not
    // lose: it has to be moved aside rather than overwritten.
    {
        Database changedDatabase(databasePath, Database::OPEN_READ_WRITE);
        changedDatabase.execute("CREATE TABLE marker (id INTEGER)");
        changedDatabase.execute("INSERT INTO marker (id) VALUES (1)");
    }
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    commandOptions.answerYesToEverything=true;
    commandOptions.backupsAction="restore";
    BackupsCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    // The database is the copy, so the marker the copy never had is gone.
    {
        Database restoredDatabase(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE_FALSE(StatisticsReader::tableExists(restoredDatabase, "marker"));
    }
    REQUIRE(countReadableTextParts(databasePath)==readablePartsBeforeRestore);
    // The file that was there before the restore was kept, and it still holds what
    // only it held.
    const std::filesystem::path folderPath=std::filesystem::path(databasePath).parent_path();
    const string databaseFileName=std::filesystem::path(databasePath).filename().string();
    bool replacedFileWasKept=false;
    std::error_code directoryError;
    std::filesystem::directory_iterator entryIterator(folderPath, directoryError);
    if (!directoryError) {
        for (const std::filesystem::directory_entry& entry : entryIterator) {
            if (entry.path().filename().string().rfind(databaseFileName + ".before-restore-", 0)==0) {
                replacedFileWasKept=true;
                Database replacedDatabase(entry.path().string(), Database::OPEN_READ_ONLY);
                REQUIRE(StatisticsReader::tableExists(replacedDatabase, "marker"));
            }
        }
    }
    REQUIRE(replacedFileWasKept);
    removeEverythingBeside(databasePath);
}
TEST_CASE("deleting refuses to guess and removes everything with --all", "[integration][backups]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BackupsDelete");
    buildDatabaseWithOneSafetyCopy(databasePath);
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).size()==1);
    // Nothing says which copies to remove, so nothing is removed.
    CommandOptions guessingOptions;
    guessingOptions.databaseFilePath=databasePath;
    guessingOptions.quiet=true;
    guessingOptions.answerYesToEverything=true;
    guessingOptions.backupsAction="delete";
    BackupsCommand refusingCommand(false);
    REQUIRE(refusingCommand.execute(guessingOptions)==2);
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).size()==1);
    // With --all the copies go, and the space is reported.
    CommandOptions allOptions;
    allOptions.databaseFilePath=databasePath;
    allOptions.quiet=true;
    allOptions.answerYesToEverything=true;
    allOptions.backupsAction="delete";
    allOptions.backupsDeleteAll=true;
    BackupsCommand deletingCommand(false);
    REQUIRE(deletingCommand.execute(allOptions)==0);
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).empty());
    removeEverythingBeside(databasePath);
}
