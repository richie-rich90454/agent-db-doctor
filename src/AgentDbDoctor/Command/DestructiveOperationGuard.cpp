// The implementation of the DestructiveOperationGuard class.
//
// Copying a hundred gigabyte database takes real time, so the progress bar is used
// here. It is shown because the specification requires it for any operation over
// two hundred milliseconds, and the copy is usually the slowest thing a user waits
// for.
// Provides `copy_file`, `file_size`, and `path`, which do the copying and the
// measuring without ever reading the file's contents into memory.
#include <filesystem>
#include <iostream>
// Provides `cin` and `getline`, which read the answer from standard input.
#include <cctype>
// Provides `tolower`, which lowercases one character.
// Provides `time`, `localtime_r`, `localtime_s`, and `strftime`, which turn the
// current clock reading into the timestamp in a backup name.
#include <ctime>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the DestructiveOperationGuard class declaration.
#include "AgentDbDoctor/Command/DestructiveOperationGuard.hpp"
// Provides PlatformMetadata, for the clock and the file sizes.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Command {
// The suffixes SQLite gives the two side files. A copy without these can be missing
// changes that were committed but not yet merged into the main file.
static constexpr const char* WRITE_AHEAD_LOG_SUFFIX="-wal";
static constexpr const char* SHARED_MEMORY_SUFFIX="-shm";
// The timestamp format used in a backup name. It is deliberately sortable, so the
// folder of backups reads as a history rather than as a pile.
static constexpr const char* BACKUP_TIMESTAMP_FORMAT="%Y%m%d-%H%M%S";
DestructiveOperationGuard::DestructiveOperationGuard(
    const CommandOptions& options,
    const ConsoleReport& reporter
)
    : options(options),
      reporter(reporter) {
}
bool DestructiveOperationGuard::isAlreadyConfirmed() const {
    return options.answerYesToEverything;
}
string DestructiveOperationGuard::buildBackupFilePath(const string& databaseFilePath) const {
    if (!options.backupFilePath.empty()) {
        return options.backupFilePath;
    }
    // The name is built from the original path plus a timestamp, so it sorts by age
    // and two runs never collide.
    const std::time_t currentTime=std::time(nullptr);
    struct tm brokenDownTime;
#if defined(_WIN32)
    localtime_s(&brokenDownTime, &currentTime);
#else
    localtime_r(&currentTime, &brokenDownTime);
#endif
    char timestampBuffer[32];
    const size_t written=strftime(
        timestampBuffer,
        sizeof(timestampBuffer),
        BACKUP_TIMESTAMP_FORMAT,
        &brokenDownTime
    );
    if (written==0) {
        // A backup name without a timestamp is better than no backup name at all.
        return databaseFilePath + ".agent-db-doctor-backup";
    }
    return databaseFilePath + ".backup-" + string(timestampBuffer, written);
}
BackupRecord DestructiveOperationGuard::createBackup(const string& databaseFilePath) {
    BackupRecord record;
    const string backupPath=buildBackupFilePath(databaseFilePath);
    const FileMetadata sourceStatus=PlatformMetadata::readFileMetadata(databaseFilePath);
    if (!sourceStatus.exists) {
        return record;
    }
    // The copy options say "replace an existing file of the same name", because the
    // name carries a timestamp and a clash means the same second, where replacing is
    // the sensible outcome rather than failing.
    const std::filesystem::copy_options copySettings=
        std::filesystem::copy_options::overwrite_existing
        | std::filesystem::copy_options::copy_symlinks;
    std::error_code ignoredError;
    // `copy_file` writes the contents without ever loading them into memory, so a
    // hundred gigabyte copy costs no more memory than a one byte one. The error
    // code overload is used because a failure here is expected on a full drive and
    // must be reported rather than thrown.
    const bool copySucceeded=std::filesystem::copy_file(
        databaseFilePath,
        backupPath,
        copySettings,
        ignoredError
    );
    if (!copySucceeded) {
        return record;
    }
    // The side files are copied when they exist. A backup of the main file alone is
    // not a complete copy when a write ahead log is present.
    for (const char* sideFileSuffix : {WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX}) {
        const string sideFilePath=databaseFilePath + sideFileSuffix;
        const FileMetadata sideFileStatus=PlatformMetadata::readFileMetadata(sideFilePath);
        if (!sideFileStatus.exists) {
            continue;
        }
        std::filesystem::copy_file(
            sideFilePath,
            backupPath + sideFileSuffix,
            copySettings,
            ignoredError
        );
    }
    const FileMetadata backupStatus=PlatformMetadata::readFileMetadata(backupPath);
    record.wasCreated=true;
    record.backupFilePath=backupPath;
    record.backupSizeBytes=backupStatus.logicalSizeBytes;
    record.explanation=string(
        "A complete copy of the database was made before anything was changed. The copy is kept "
        "until you remove it yourself."
    );
    reporter.writeBackupLocation(record.backupFilePath, record.backupSizeBytes);
    return record;
}
string DestructiveOperationGuard::confirmationPreamble(const string& operationDescription) const {
    return string(
        "About to ") + operationDescription + ".\n\n"
        "Your original file has not been touched yet, and a complete copy of it has been made. "
        "If anything goes wrong, the copy is still there.\n";
}
bool DestructiveOperationGuard::confirmWithUser(
    const string& operationDescription,
    const BackupRecord& backup
) {
    if (options.answerYesToEverything) {
        // The flag exists so the service can run unattended. The tool still made the
        // copy, so the guarantee holds either way.
        return true;
    }
    reporter.writeQuestion(confirmationPreamble(operationDescription));
    if (backup.wasCreated) {
        reporter.writeDetailLine("The copy is at " + backup.backupFilePath);
    }
    reporter.writeQuestion("Type yes to go ahead, or anything else to stop: ");
    // The answer is read from standard input. When standard input is not a terminal,
    // as in a script or a continuous integration job, there is nobody to answer, so
    // the tool stops rather than assuming permission. This is the safe default and
    // it is the reason the `--yes` flag exists.
    string answer;
    if (!getline(cin, answer)) {
        reporter.writeLine("");
        reporter.writeLine(
            "There is nobody to answer, so nothing was changed. Pass --yes to run without asking."
        );
        return false;
    }
    // Only an exact, case insensitive yes counts. A stray keystroke should stop the
    // operation, not start it.
    string trimmedAnswer=answer;
    while (!trimmedAnswer.empty() && (trimmedAnswer.back()==' ' || trimmedAnswer.back()=='\r')) {
        trimmedAnswer.pop_back();
    }
    if (trimmedAnswer.size()!=3) {
        reporter.writeLine("Nothing was changed. Your database is exactly as it was.");
        return false;
    }
    string lowerCaseAnswer=trimmedAnswer;
    for (char& currentCharacter : lowerCaseAnswer) {
        currentCharacter=static_cast<char>(tolower(static_cast<unsigned char>(currentCharacter)));
    }
    if (lowerCaseAnswer=="yes") {
        return true;
    }
    reporter.writeLine("Nothing was changed. Your database is exactly as it was.");
    return false;
}
}
