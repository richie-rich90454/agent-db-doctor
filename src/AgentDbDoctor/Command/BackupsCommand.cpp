// The implementation of the BackupsCommand class.
//
// The four actions are deliberately small and separate. `list` only reads names
// and sizes. `verify` copies a backup to a temporary name beside it, checks it
// there, and removes the temporary file, which is the only way to know a copy is
// restorable rather than merely present. `restore` never deletes the file it
// replaces: the current database is moved aside first. `delete` names exactly
// what it removes and asks before removing it.
// Provides `rename` and `remove`, which move files aside and clear them away.
#include <filesystem>
// Provides `cin` and `getline`, which read the answer to the confirmation.
#include <iostream>
// Provides the `tolower` used by the confirmation comparison.
#include <cctype>
// Provides `time`, `localtime`, and `strftime`, used in the names this command makes.
#include <ctime>
// Provides `sort`, which orders the copies newest first.
#include <algorithm>
// Provides the wall clock reading used for the age of a copy.
#include <chrono>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the BackupsCommand class declaration.
#include "AgentDbDoctor/Command/BackupsCommand.hpp"
// Provides the guard, whose naming rule is the single definition of a backup name.
#include "AgentDbDoctor/Command/DestructiveOperationGuard.hpp"
// Provides PlatformMetadata, for the sizes, the clone attempt, and the clock.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the Database wrapper, used to check a copy.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides StatisticsReader, for the session count of a copy.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides SafetyChecks, for the integrity check of a copy.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
// Provides the AgentRegistry, used to find the databases when none is named.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Command {
// The suffixes SQLite gives the side files of a database. A copy carries them too,
// so a copy is only complete when they travel with it.
static constexpr const char* SIDE_FILE_SUFFIXES[]={"-wal", "-shm", "-journal"};
// The suffix given to the temporary file a restore drill is performed on.
static constexpr const char* DRILL_FILE_SUFFIX=".verify-restore";
// The suffix given to the database file while it is moved aside before a restore.
static constexpr const char* REPLACED_FILE_SUFFIX=".before-restore-";
// Builds a name of the form `base` plus a sortable timestamp.
static string buildTimestampedName(const string& base) {
    const std::time_t currentTime=std::time(nullptr);
    struct tm brokenDownTime;
#if defined(_WIN32)
    localtime_s(&brokenDownTime, &currentTime);
#else
    localtime_r(&currentTime, &brokenDownTime);
#endif
    char timestampBuffer[32];
    const size_t written=strftime(timestampBuffer, sizeof(timestampBuffer), "%Y%m%d-%H%M%S", &brokenDownTime);
    if (written==0) {
        return base;
    }
    return base + string(timestampBuffer, written);
}
// The age of a file as a short sentence, for the listing.
static string formatAge(long long modificationEpochMilliseconds) {
    const long long nowMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const long long ageSeconds=(nowMilliseconds-modificationEpochMilliseconds)/1000;
    if (ageSeconds<60) {
        return string("just now");
    }
    if (ageSeconds<3600) {
        return to_string(ageSeconds/60) + " minutes old";
    }
    if (ageSeconds<86400) {
        return to_string(ageSeconds/3600) + " hours old";
    }
    return to_string(ageSeconds/86400) + " days old";
}
// The age of a file in whole days, used by `delete --older-than`.
static long long ageInDays(long long modificationEpochMilliseconds) {
    const long long nowMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return (nowMilliseconds-modificationEpochMilliseconds)/86400000LL;
}
// What one restore drill found.
struct DrillResult final {
    // True when the copy passed its check and can be put back.
    bool isRestorable=false;
    // A sentence describing what was found.
    string explanation;
    // The number of sessions the copy holds, or zero when it holds none or could
    // not be asked.
    long long sessionCount=0;
};
// Copies one backup to a temporary name, checks it there, and removes the
// temporary file. Checking the copy rather than the original is the whole point:
// it is the copy that would be restored.
static DrillResult runRestoreDrill(const BackupFile& backup) {
    DrillResult result;
    const string drillFilePath=backup.filePath + DRILL_FILE_SUFFIX;
    std::error_code removalError;
    std::filesystem::remove(drillFilePath, removalError);
    if (removalError) {
        result.explanation="a leftover file from an earlier drill is in the way: " + drillFilePath;
        return result;
    }
    bool wasCopied=PlatformMetadata::tryCreateCopyOnWriteClone(backup.filePath, drillFilePath);
    if (!wasCopied) {
        std::error_code copyError;
        wasCopied=std::filesystem::copy_file(
            backup.filePath,
            drillFilePath,
            std::filesystem::copy_options::overwrite_existing,
            copyError
        );
    }
    if (!wasCopied) {
        result.explanation="the copy could not be read: " + backup.filePath;
        return result;
    }
    try {
        Database drillDatabase(drillFilePath, Database::OPEN_READ_ONLY);
        drillDatabase.setBusyTimeoutMilliseconds(2000);
        const SafetyCheckResult damageCheck=SafetyChecks::verifyDatabaseIsNotDamaged(drillDatabase);
        if (!damageCheck.passed()) {
            result.explanation=damageCheck.explanation();
        } else {
            result.isRestorable=true;
            result.explanation="its integrity check passed";
            if (StatisticsReader::tableExists(drillDatabase, "session")) {
                result.sessionCount=StatisticsReader::exactRowCount(drillDatabase, "session");
            }
        }
    } catch (const DatabaseOpenException& openFailure) {
        result.explanation="it could not be opened as a database: " + openFailure.message();
    } catch (const DatabaseOperationException& operationFailure) {
        result.explanation=operationFailure.message();
    }
    std::error_code cleanupError;
    std::filesystem::remove(drillFilePath, cleanupError);
    if (cleanupError) {
        result.isRestorable=false;
        result.explanation+="; the temporary copy could not be removed afterwards and is at " + drillFilePath;
    }
    return result;
}
BackupsCommand::BackupsCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string BackupsCommand::describe() const {
    return string(
        "List, verify, restore, or delete the safety copies the tool makes before a destructive "
        "run. Verification copies a backup beside itself, checks the copy, and removes it again, "
        "which proves the copy is restorable without touching the database."
    );
}
string BackupsCommand::commandName() const {
    return string("backups");
}
vector<BackupFile> BackupsCommand::findBackupsBeside(const string& databaseFilePath) {
    vector<BackupFile> backups;
    const std::filesystem::path databasePath(databaseFilePath);
    const std::filesystem::path folderPath=databasePath.parent_path();
    if (folderPath.empty()) {
        return backups;
    }
    std::error_code directoryError;
    std::filesystem::directory_iterator entryIterator(folderPath, directoryError);
    if (directoryError) {
        return backups;
    }
    for (const std::filesystem::directory_entry& entry : entryIterator) {
        const string fileName=entry.path().filename().string();
        if (!DestructiveOperationGuard::isBackupFileNameFor(databaseFilePath, fileName)) {
            continue;
        }
        // A side file of a copy shares the copy's prefix, so it is skipped here; its
        // bytes are counted into the copy it belongs to instead.
        bool isSideFile=false;
        for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
            const string suffix(sideFileSuffix);
            if (fileName.size()>suffix.size()
                && fileName.compare(fileName.size()-suffix.size(), suffix.size(), suffix)==0) {
                isSideFile=true;
                break;
            }
        }
        if (isSideFile) {
            continue;
        }
        BackupFile backup;
        backup.filePath=entry.path().string();
        const FileMetadata mainFileStatus=PlatformMetadata::readFileMetadata(backup.filePath);
        backup.modificationEpochMilliseconds=mainFileStatus.modificationEpochMilliseconds;
        long long sizeBytes=mainFileStatus.logicalSizeBytes;
        for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
            const FileMetadata sideFileStatus=PlatformMetadata::readFileMetadata(backup.filePath + sideFileSuffix);
            if (sideFileStatus.exists) {
                sizeBytes+=sideFileStatus.logicalSizeBytes;
            }
        }
        backup.sizeBytes=sizeBytes;
        backups.push_back(backup);
    }
    sort(
        backups.begin(),
        backups.end(),
        [](const BackupFile& left, const BackupFile& right) {
            if (left.modificationEpochMilliseconds!=right.modificationEpochMilliseconds) {
                return left.modificationEpochMilliseconds>right.modificationEpochMilliseconds;
            }
            return left.filePath>right.filePath;
        }
    );
    return backups;
}
vector<string> BackupsCommand::targetDatabasePaths(const CommandOptions& options, bool discoveryIsAllowed) const {
    vector<string> databasePaths;
    if (!options.databaseFilePath.empty()) {
        databasePaths.push_back(options.databaseFilePath);
        return databasePaths;
    }
    if (!discoveryIsAllowed) {
        return databasePaths;
    }
    const AgentRegistry registry;
    for (const DiscoveredDatabase& discovered : registry.discoverAllDatabases()) {
        // Only real databases have safety copies; a folder of text files has no
        // file beside it that one could have been written next to.
        if (discovered.isSqliteDatabase) {
            databasePaths.push_back(discovered.filePath);
        }
    }
    return databasePaths;
}
bool BackupsCommand::chooseBackup(
    const CommandOptions& options,
    const string& databaseFilePath,
    BackupFile& chosenOut
) const {
    if (!options.backupsFilePath.empty()) {
        const FileMetadata chosenStatus=PlatformMetadata::readFileMetadata(options.backupsFilePath);
        if (!chosenStatus.exists) {
            return false;
        }
        chosenOut.filePath=options.backupsFilePath;
        chosenOut.sizeBytes=chosenStatus.logicalSizeBytes;
        chosenOut.modificationEpochMilliseconds=chosenStatus.modificationEpochMilliseconds;
        return true;
    }
    const vector<BackupFile> backups=findBackupsBeside(databaseFilePath);
    if (backups.empty()) {
        return false;
    }
    chosenOut=backups.front();
    return true;
}
bool BackupsCommand::confirmWithUser(const CommandOptions& options, const string& questionText) const {
    if (options.answerYesToEverything) {
        return true;
    }
    reporter.writeQuestion(questionText);
    reporter.writeQuestion("Type yes to go ahead, or anything else to stop: ");
    string answer;
    if (!getline(cin, answer)) {
        reporter.writeLine("");
        reporter.writeLine(
            "There is nobody to answer, so nothing was changed. Pass --yes to run without asking."
        );
        return false;
    }
    string trimmedAnswer=answer;
    while (!trimmedAnswer.empty() && (trimmedAnswer.back()==' ' || trimmedAnswer.back()=='\r')) {
        trimmedAnswer.pop_back();
    }
    if (trimmedAnswer.size()!=3) {
        return false;
    }
    string lowerCaseAnswer=trimmedAnswer;
    for (char& currentCharacter : lowerCaseAnswer) {
        currentCharacter=static_cast<char>(tolower(static_cast<unsigned char>(currentCharacter)));
    }
    return lowerCaseAnswer=="yes";
}
int BackupsCommand::listBackups(const CommandOptions& options) {
    const vector<string> databasePaths=targetDatabasePaths(options, true);
    if (databasePaths.empty()) {
        reporter.writeLine(
            "No supported coding agent has a database on this machine, so there is nothing to "
            "look beside. If you know the path, pass it with --database."
        );
        return 0;
    }
    long long totalCopyCount=0;
    long long totalBytes=0;
    reporter.writeHeading("Safety copies");
    for (const string& databaseFilePath : databasePaths) {
        const vector<BackupFile> backups=findBackupsBeside(databaseFilePath);
        reporter.writeLine(databaseFilePath);
        if (backups.empty()) {
            reporter.writeDetailLine("No safety copies were found beside this database.");
            continue;
        }
        for (const BackupFile& backup : backups) {
            reporter.writeDetailLine(
                "  " + backup.filePath + "  " + HumanReadableFormatting::formatBytes(backup.sizeBytes)
                + "  created " + HumanReadableFormatting::formatTimestamp(backup.modificationEpochMilliseconds)
                + " (" + formatAge(backup.modificationEpochMilliseconds) + ")"
            );
            totalCopyCount++;
            totalBytes+=backup.sizeBytes;
        }
    }
    reporter.writeLine("");
    reporter.writeLine(
        to_string(totalCopyCount) + (totalCopyCount==1 ? " safety copy occupies " : " safety copies occupy ")
        + HumanReadableFormatting::formatBytes(totalBytes) + " in total."
    );
    reporter.writeSuggestedNextSteps(vector<string>{
        string("Prove a copy can actually be put back: agent-db-doctor backups verify --database <path>"),
        string("Put a copy back over the database: agent-db-doctor backups restore --database <path>"),
        string("Hand back the space old copies occupy: agent-db-doctor backups delete --older-than 7 --database <path>")
    });
    return 0;
}
int BackupsCommand::verifyBackups(const CommandOptions& options) {
    vector<BackupFile> backupsToVerify;
    if (!options.backupsFilePath.empty()) {
        BackupFile namedBackup;
        const FileMetadata namedBackupStatus=PlatformMetadata::readFileMetadata(options.backupsFilePath);
        if (!namedBackupStatus.exists) {
            reporter.writeFailure(
                string("There is no file at ") + options.backupsFilePath + ".", string()
            );
            return 1;
        }
        namedBackup.filePath=options.backupsFilePath;
        namedBackup.sizeBytes=namedBackupStatus.logicalSizeBytes;
        namedBackup.modificationEpochMilliseconds=namedBackupStatus.modificationEpochMilliseconds;
        backupsToVerify.push_back(namedBackup);
    } else {
        const vector<string> databasePaths=targetDatabasePaths(options, false);
        if (databasePaths.empty()) {
            reporter.writeFailure(
                string("This action needs to know which database to look beside. Tell it with "
                    "--database, or name a copy directly with --file."),
                string()
            );
            return 2;
        }
        for (const string& databaseFilePath : databasePaths) {
            const vector<BackupFile> backups=findBackupsBeside(databaseFilePath);
            if (backups.empty()) {
                reporter.writeLine("No safety copies were found beside " + databaseFilePath + ".");
                continue;
            }
            if (options.backupsDeleteAll) {
                backupsToVerify.insert(backupsToVerify.end(), backups.begin(), backups.end());
            } else {
                // The newest copy is the one a restore would pick, so it is the one
                // whose restorability matters most.
                backupsToVerify.push_back(backups.front());
            }
        }
    }
    if (backupsToVerify.empty()) {
        reporter.writeLine("There was no safety copy to test.");
        return 0;
    }
    reporter.writeHeading("Restore drill");
    bool everyCopyIsRestorable=true;
    for (const BackupFile& backup : backupsToVerify) {
        const DrillResult drill=runRestoreDrill(backup);
        everyCopyIsRestorable=everyCopyIsRestorable && drill.isRestorable;
        const string verdict=drill.isRestorable ? "restorable" : "NOT restorable";
        string line=string("  ") + backup.filePath + "  " + verdict + ": " + drill.explanation;
        if (drill.isRestorable && drill.sessionCount>0) {
            line+=", holding " + HumanReadableFormatting::formatCount(drill.sessionCount) + " sessions";
        }
        line+=".";
        reporter.writeLine(line);
    }
    reporter.writeLine("");
    if (everyCopyIsRestorable) {
        reporter.writeLine(
            string("Every tested copy was put back onto a scratch file and checked there, so it "
                "can be put back for real.")
        );
        return 0;
    }
    reporter.writeFailure(
        string("At least one copy did not pass the drill. A copy that cannot be restored is not a "
            "safety net; make a fresh one while the database still reads well."),
        string()
    );
    return 1;
}
int BackupsCommand::restoreChosenBackup(const CommandOptions& options) {
    const vector<string> databasePaths=targetDatabasePaths(options, false);
    if (databasePaths.empty()) {
        reporter.writeFailure(
            string("This action needs to know which database to put a copy back over. Tell it "
                "with --database."),
            string()
        );
        return 2;
    }
    const string databaseFilePath=databasePaths.front();
    BackupFile chosenBackup;
    if (!chooseBackup(options, databaseFilePath, chosenBackup)) {
        reporter.writeFailure(
            string("There is no safety copy beside ") + databaseFilePath
            + " to put back. If the copy lives elsewhere, name it with --file.",
            string()
        );
        return 1;
    }
    const FileMetadata currentStatus=PlatformMetadata::readFileMetadata(databaseFilePath);
    const string replacedFilePath=currentStatus.exists
        ? buildTimestampedName(databaseFilePath + REPLACED_FILE_SUFFIX)
        : string();
    string questionText;
    if (currentStatus.exists) {
        questionText=string("This puts the safety copy back over the database:\n  ") + chosenBackup.filePath
            + "\n\nThe file that is there now is moved aside first, to:\n  " + replacedFilePath
            + "\n\nso nothing is lost either way.\n\nType yes to go ahead.";
    } else {
        questionText=string("The database file is not there, so the copy will be put in its place:\n  ")
            + chosenBackup.filePath + "\n\nType yes to go ahead.";
    }
    if (!confirmWithUser(options, questionText)) {
        reporter.writeLine("Nothing was changed.");
        return 0;
    }
    if (currentStatus.exists) {
        std::error_code displacementError;
        std::filesystem::rename(databaseFilePath, replacedFilePath, displacementError);
        if (!displacementError) {
            for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
                const string sideFilePath=databaseFilePath + sideFileSuffix;
                if (PlatformMetadata::readFileMetadata(sideFilePath).exists) {
                    std::filesystem::rename(sideFilePath, replacedFilePath + sideFileSuffix, displacementError);
                    if (displacementError) {
                        break;
                    }
                }
            }
        }
        if (displacementError) {
            reporter.writeFailure(
                string("The file that is there now could not be moved aside, so nothing was "
                    "changed. This usually means the coding agent still has it open."),
                string()
            );
            return 1;
        }
    }
    // The guard owns the restore: it removes the current side files first, copies
    // the main file and the copy's side files back, and prints what it did.
    BackupRecord restoreRecord;
    restoreRecord.wasCreated=true;
    restoreRecord.backupFilePath=chosenBackup.filePath;
    restoreRecord.backupSizeBytes=chosenBackup.sizeBytes;
    restoreRecord.copiedBytes=chosenBackup.sizeBytes;
    DestructiveOperationGuard restoringGuard(options, reporter);
    if (!restoringGuard.restoreBackup(restoreRecord, databaseFilePath)) {
        reporter.writeFailure(
            string("The copy could not be written back over the database. It is untouched at ")
            + chosenBackup.filePath
            + (currentStatus.exists ? ", and the previous file is at " + replacedFilePath : string())
            + ".",
            string()
        );
        return 1;
    }
    // The restored file is checked before it is presented as recovered.
    bool restoredDatabaseIsSound=false;
    long long restoredSessionCount=0;
    try {
        Database restoredDatabase(databaseFilePath, Database::OPEN_READ_ONLY);
        restoredDatabase.setBusyTimeoutMilliseconds(5000);
        const SafetyCheckResult damageCheck=SafetyChecks::verifyDatabaseIsNotDamaged(restoredDatabase);
        restoredDatabaseIsSound=damageCheck.passed();
        if (restoredDatabaseIsSound && StatisticsReader::tableExists(restoredDatabase, "session")) {
            restoredSessionCount=StatisticsReader::exactRowCount(restoredDatabase, "session");
        }
    } catch (const DatabaseOpenException&) {
        restoredDatabaseIsSound=false;
    }
    reporter.writeHeading("Done");
    reporter.writeLine("Put the safety copy back over the database.");
    if (currentStatus.exists) {
        reporter.writeLine("The file that was there before is at " + replacedFilePath + ".");
    }
    if (restoredDatabaseIsSound) {
        reporter.writeLine(
            string("The restored database passed its integrity check")
            + (restoredSessionCount>0
                ? " and holds " + HumanReadableFormatting::formatCount(restoredSessionCount) + " sessions."
                : ".")
        );
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Run the diagnose command to see the database as it now stands."),
            string("Once you are satisfied, delete the copy and the replaced file to get their space back.")
        });
        return 0;
    }
    reporter.writeFailure(
        string("The copy is in place, but the restored database did not pass its integrity check. "
            "Run the recover command for the safe next step; the file that was there before is "
            "still untouched."),
        string()
    );
    return 1;
}
int BackupsCommand::deleteBackups(const CommandOptions& options) {
    const vector<string> databasePaths=targetDatabasePaths(options, false);
    if (databasePaths.empty()) {
        reporter.writeFailure(
            string("This action needs to know which database's copies to remove. Tell it with "
                "--database. Copies are never removed across every agent at once."),
            string()
        );
        return 2;
    }
    if (!options.backupsDeleteAll && options.backupsOlderThanDays<0) {
        reporter.writeFailure(
            string("Nothing says which copies to remove. Name an age with --older-than DAYS, or "
                "say every copy is meant with --all."),
            string()
        );
        return 2;
    }
    vector<BackupFile> copiesToRemove;
    for (const string& databaseFilePath : databasePaths) {
        for (const BackupFile& backup : findBackupsBeside(databaseFilePath)) {
            if (options.backupsDeleteAll
                || ageInDays(backup.modificationEpochMilliseconds)>=options.backupsOlderThanDays) {
                copiesToRemove.push_back(backup);
            }
        }
    }
    if (copiesToRemove.empty()) {
        reporter.writeLine("No safety copy matched, so there was nothing to remove.");
        return 0;
    }
    long long totalBytes=0;
    string questionText=string("This removes ")
        + to_string(copiesToRemove.size())
        + (copiesToRemove.size()==1 ? " safety copy:\n" : " safety copies:\n");
    for (const BackupFile& backup : copiesToRemove) {
        questionText+="  " + backup.filePath + "\n";
        totalBytes+=backup.sizeBytes;
    }
    questionText+="\nThey occupy " + HumanReadableFormatting::formatBytes(totalBytes)
        + " in total. A safety copy is the way back if something turns out to be wrong with the "
          "database, so remove them only when you are satisfied with its current state.\n\n"
          "Type yes to go ahead.";
    if (!confirmWithUser(options, questionText)) {
        reporter.writeLine("Nothing was changed.");
        return 0;
    }
    long long handedBackBytes=0;
    int failedRemovalCount=0;
    for (const BackupFile& backup : copiesToRemove) {
        std::error_code removalError;
        if (!std::filesystem::remove(backup.filePath, removalError) || removalError) {
            failedRemovalCount++;
            reporter.writeLine("Could not remove " + backup.filePath + ".");
            continue;
        }
        handedBackBytes+=backup.sizeBytes;
        for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
            std::error_code sideFileRemovalError;
            std::filesystem::remove(backup.filePath + sideFileSuffix, sideFileRemovalError);
        }
    }
    reporter.writeHeading("Done");
    const long long removedCopyCount=
        static_cast<long long>(copiesToRemove.size()) - failedRemovalCount;
    reporter.writeLine(
        string("Removed ") + to_string(removedCopyCount) + " of "
        + to_string(copiesToRemove.size()) + " safety copies."
    );
    reporter.writeLine(
        string("Handed back to the drive: ") + HumanReadableFormatting::formatBytes(handedBackBytes) + "."
    );
    return failedRemovalCount>0 ? 1 : 0;
}
int BackupsCommand::execute(const CommandOptions& options) {
    const string action=options.backupsAction.empty() ? string("list") : options.backupsAction;
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (action=="list") {
        return listBackups(options);
    }
    if (action=="verify") {
        return verifyBackups(options);
    }
    if (action=="restore") {
        return restoreChosenBackup(options);
    }
    if (action=="delete") {
        return deleteBackups(options);
    }
    reporter.writeFailure(
        string("There is no `backups ") + action + "` action. The actions are: list, verify, "
        "restore, delete.",
        string()
    );
    return 2;
}
}
