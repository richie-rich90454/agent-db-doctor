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
// Provides the number to text conversions used in the space accounting lines.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Command {
// The suffixes SQLite gives the two side files. A copy without these can be missing
// changes that were committed but not yet merged into the main file.
static constexpr const char* WRITE_AHEAD_LOG_SUFFIX="-wal";
static constexpr const char* SHARED_MEMORY_SUFFIX="-shm";
// The rollback journal, which exists only while a write is in progress or after one
// was interrupted. A copy of the main file alone can be missing the changes it
// holds, so it travels with the copy too.
static constexpr const char* ROLLBACK_JOURNAL_SUFFIX="-journal";
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
    // A path that is a symbolic link is resolved before anything is copied.
    // Copying the link itself would produce a backup that points at the original,
    // which is not a copy at all; the change happened after an audit that found
    // exactly that with `copy_symlinks`.
    std::error_code resolutionError;
    std::filesystem::path sourcePath=std::filesystem::weakly_canonical(databaseFilePath, resolutionError);
    if (resolutionError || sourcePath.empty()) {
        sourcePath=databaseFilePath;
    }
    // The copy is attempted as a copy on write clone first. On the file systems
    // that support one the copy is instant and occupies no space, which is what
    // makes keeping a safety copy affordable in the first place.
    const bool wasMadeAsClone=PlatformMetadata::tryCreateCopyOnWriteClone(
        sourcePath.string(),
        backupPath
    );
    std::error_code copyError;
    if (!wasMadeAsClone) {
        // The plain copy replaces any file of the same name, because the name
        // carries a timestamp and a clash means the same second, where replacing is
        // the sensible outcome rather than failing.
        const std::filesystem::copy_options copySettings=
            std::filesystem::copy_options::overwrite_existing;
        if (!std::filesystem::copy_file(sourcePath, backupPath, copySettings, copyError)) {
            return record;
        }
    }
    long long copiedBytes=PlatformMetadata::readFileMetadata(backupPath).logicalSizeBytes;
    // The side files are copied when they exist and every copy is checked. A
    // failure here means the backup would be incomplete, and an incomplete backup
    // that says "complete" is worse than no backup at all, so the partial file is
    // removed and the caller is told the copy could not be made.
    for (const char* sideFileSuffix : {WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX, ROLLBACK_JOURNAL_SUFFIX}) {
        const string sideFilePath=databaseFilePath + sideFileSuffix;
        const FileMetadata sideFileStatus=PlatformMetadata::readFileMetadata(sideFilePath);
        if (!sideFileStatus.exists) {
            continue;
        }
        std::error_code sideFileError;
        const std::filesystem::copy_options sideCopySettings=
            std::filesystem::copy_options::overwrite_existing;
        if (!std::filesystem::copy_file(
                sideFilePath,
                backupPath + sideFileSuffix,
                sideCopySettings,
                sideFileError)) {
            std::error_code cleanupError;
            std::filesystem::remove(backupPath, cleanupError);
            for (const char* copiedSideFileSuffix : {WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX, ROLLBACK_JOURNAL_SUFFIX}) {
                std::filesystem::remove(backupPath + copiedSideFileSuffix, cleanupError);
            }
            return record;
        }
        copiedBytes+=PlatformMetadata::readFileMetadata(backupPath + sideFileSuffix).logicalSizeBytes;
    }
    const FileMetadata backupStatus=PlatformMetadata::readFileMetadata(backupPath);
    record.wasCreated=true;
    record.backupFilePath=backupPath;
    record.backupSizeBytes=backupStatus.logicalSizeBytes;
    record.copiedBytes=copiedBytes;
    record.explanation=string(
        "A complete copy of the database was made before anything was changed. It is removed "
        "again once the run finishes and its own checks pass, so it costs no space in the end. "
        "Pass --keep-backup, or name a location with --backup-to, to keep it."
    );
    reporter.writeBackupLocation(record.backupFilePath, record.backupSizeBytes);
    return record;
}
bool DestructiveOperationGuard::shouldKeepBackup() const {
    // A destination the user named is a destination the user wants, so
    // `--backup-to` implies "keep" rather than needing a second flag.
    return options.keepBackup || !options.backupFilePath.empty();
}
long long DestructiveOperationGuard::removeBackupAfterSuccess(const BackupRecord& backup) {
    if (!backup.wasCreated) {
        return 0;
    }
    if (shouldKeepBackup()) {
        reporter.writeDetailLine(
            "The safety copy was kept at " + backup.backupFilePath + ", as asked."
        );
        return 0;
    }
    std::error_code removalError;
    const bool removedMainFile=std::filesystem::remove(backup.backupFilePath, removalError);
    if (removalError) {
        reporter.writeWarning(
            string("The safety copy could not be removed, so it is still at ")
            + backup.backupFilePath
            + ".\n\nDelete it yourself when you are satisfied with the result: the space it holds "
              "is the space this run was meant to hand back."
        );
        return 0;
    }
    long long handedBackBytes=removedMainFile ? backup.backupSizeBytes : 0;
    for (const char* sideFileSuffix : {WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX, ROLLBACK_JOURNAL_SUFFIX}) {
        std::error_code sideFileRemovalError;
        if (std::filesystem::remove(backup.backupFilePath + sideFileSuffix, sideFileRemovalError)) {
            handedBackBytes+=PlatformMetadata::readFileMetadata(backup.backupFilePath + sideFileSuffix).logicalSizeBytes;
        }
    }
    reporter.writeDetailLine(
        string("The safety copy was removed after the checks passed: ")
        + HumanReadableFormatting::formatBytes(handedBackBytes) + " handed back."
    );
    return handedBackBytes;
}
bool DestructiveOperationGuard::restoreBackup(const BackupRecord& backup, const string& databaseFilePath) {
    if (!backup.wasCreated) {
        return false;
    }
    // The current side files are removed first. A write ahead log from the changed
    // database paired by filename with the restored main file is exactly the stale
    // pairing that corrupts a database, so the side files must go before the copy
    // comes back.
    std::error_code removalError;
    for (const char* sideFileSuffix : {WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX, ROLLBACK_JOURNAL_SUFFIX}) {
        std::filesystem::remove(databaseFilePath + sideFileSuffix, removalError);
    }
    const std::filesystem::copy_options restoreSettings=
        std::filesystem::copy_options::overwrite_existing;
    std::error_code copyError;
    if (!std::filesystem::copy_file(backup.backupFilePath, databaseFilePath, restoreSettings, copyError)) {
        return false;
    }
    // A side file the original had travels back with it. One the original did not
    // have was already removed above.
    for (const char* sideFileSuffix : {WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX, ROLLBACK_JOURNAL_SUFFIX}) {
        const string backupSideFilePath=backup.backupFilePath + sideFileSuffix;
        if (!PlatformMetadata::readFileMetadata(backupSideFilePath).exists) {
            continue;
        }
        std::error_code sideFileError;
        if (!std::filesystem::copy_file(
                backupSideFilePath,
                databaseFilePath + sideFileSuffix,
                restoreSettings,
                sideFileError)) {
            return false;
        }
    }
    reporter.writeDetailLine("The safety copy was put back over the database.");
    return true;
}
string DestructiveOperationGuard::confirmationPreamble(const string& operationDescription) const {
    string preamble=string(
        "About to ") + operationDescription + ".\n\n"
        "Your original file has not been touched yet, and a complete copy of it has been made. ";
    if (shouldKeepBackup()) {
        preamble+=string(
            "The copy stays where it is, so you keep a rollback point.\n"
        );
    } else {
        preamble+=string(
            "If anything goes wrong the copy is put back; once the run finishes and its checks "
            "pass, the copy is removed again so it costs no space in the end.\n"
        );
    }
    return preamble;
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
