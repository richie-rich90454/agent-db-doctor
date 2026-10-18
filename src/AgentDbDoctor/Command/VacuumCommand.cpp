// The implementation of the VacuumCommand class.
//
// The safety properties this file maintains are: the original file is never opened
// for writing, the new file is built beside it and only swapped in once it has been
// verified, the free space needed is stated before the run starts, and no file
// system compression is used at any point.
// Provides `rename` and `remove`, which swap the new file into place and clear away
// the old one.
#include <filesystem>
// Provides `ofstream` and `ifstream`, used by the interrupted swap journal.
#include <fstream>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the VacuumCommand class declaration.
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
// Provides PlatformMetadata, for the sizes, the free space, and the clock.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides DiskSpaceChecker, for the space arithmetic.
#include "AgentDbDoctor/FileSystem/DiskSpaceChecker.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides StatisticsReader, for the header and the counts.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides SafetyChecks, used for the lock check before anything is written.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
// Provides the ProgressBar class, for the rewrite phase.
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Command {
// Wraps `text` in single quotes for use inside a SQL statement, doubling any single
// quote it contains. That is the rule SQLite follows for a string literal, and it is
// why a path can be placed in the text of a statement that does not accept a bound
// parameter without becoming a way to run someone else's SQL.
static string toSqlStringLiteral(const string& text) {
    string quotedText="'";
    for (const char currentCharacter : text) {
        if (currentCharacter=='\'') {
            quotedText.push_back('\'');
        }
        quotedText.push_back(currentCharacter);
    }
    quotedText.push_back('\'');
    return quotedText;
}
// The pragma that performs the rewrite. `VACUUM INTO` writes a fresh copy holding
// only the live pages, and it never touches the file it was given. That is why the
// tool uses it rather than a plain `VACUUM`, which rewrites the original in place.
static constexpr const char* VACUUM_INTO_PRAGMA="VACUUM INTO";
// The suffix given to the new file while it is being built, so a half written file
// can never be mistaken for a finished one.
static constexpr const char* REWRITTEN_FILE_SUFFIX=".vacuuming";
// The quick check pragma, run against the new file before it is swapped in.
static constexpr const char* QUICK_CHECK_PRAGMA="PRAGMA quick_check";
// The suffix given to the interrupted swap journal. The file holds the three paths
// the swap touches, and its presence means a previous run stopped between two
// renames and must be resolved before another swap starts.
static constexpr const char* SWAP_INTENT_SUFFIX=".vacuum-swap-intent";
// The suffix the original is moved to while the new file takes its place.
static constexpr const char* DISPLACED_FILE_SUFFIX=".before-vacuum";
// The suffixes SQLite gives the side files that belong to a database. They belong
// to whichever database file they were created beside, so they travel with the
// original when it is moved aside, and they are removed with it when it is.
static constexpr const char* WRITE_AHEAD_LOG_SUFFIX="-wal";
static constexpr const char* SHARED_MEMORY_SUFFIX="-shm";
static constexpr const char* ROLLBACK_JOURNAL_SUFFIX="-journal";
// The three side file suffixes, in the order every loop over them uses.
static constexpr const char* SIDE_FILE_SUFFIXES[]={
    WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX, ROLLBACK_JOURNAL_SUFFIX
};
// Writes the journal that makes the three renames recoverable. Each path goes on
// one line, in the order the swap uses them: the database, the name the original is
// moved to, and the name the new file is built under. Returns false when the
// journal cannot be written, in which case the run must not start.
static bool writeSwapIntent(
    const string& databaseFilePath,
    const string& displacedFilePath,
    const string& rewrittenFilePath
) {
    std::ofstream intentFile(databaseFilePath + SWAP_INTENT_SUFFIX, std::ios::trunc);
    if (!intentFile.is_open()) {
        return false;
    }
    intentFile << databaseFilePath << "\n" << displacedFilePath << "\n" << rewrittenFilePath << "\n";
    return intentFile.good();
}
// Removes the journal once the swap has finished or been rolled back.
static void clearSwapIntent(const string& databaseFilePath) {
    std::error_code removalError;
    std::filesystem::remove(databaseFilePath + SWAP_INTENT_SUFFIX, removalError);
}
VacuumCommand::VacuumCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string VacuumCommand::describe() const {
    return string(
        "Vacuum the database: rewrite it so it only occupies the space it actually needs, and give "
        "the rest back to the drive. The original is never modified in place: a new file is built "
        "beside it, checked, and only then swapped in."
    );
}
string VacuumCommand::commandName() const {
    return string("vacuum");
}
const VacuumOutcome& VacuumCommand::lastOutcome() const {
    return lastRunOutcome;
}
void VacuumCommand::writePurpose(const CommandOptions& options) const {
    reporter.writeLine("Vacuuming the database: rewriting it so it gives unused space back to the drive.");
    if (options.databaseFilePath.empty()) {
        return;
    }
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForCompaction(
        fileStatus.logicalSizeBytes
    );
    // The space requirement is stated before anything starts, because a user on a
    // nearly full drive needs to know this is the step that could fill it.
    reporter.writeDetailLine(
        string("This database is ") + HumanReadableFormatting::formatBytes(fileStatus.logicalSizeBytes)
        + " and needs about " + HumanReadableFormatting::formatBytes(requirement.requiredBytes)
        + " of free space while it works, because it writes a new file before it replaces the old one."
    );
    const DiskSpace availableSpace=PlatformMetadata::readDiskSpace(options.databaseFilePath);
    reporter.writeDetailLine("Right now the drive has " + HumanReadableFormatting::formatBytes(availableSpace.availableBytes) + " free.");
}
void VacuumCommand::resolveInterruptedVacuumSwap(const string& databaseFilePath) const {
    const string intentFilePath=databaseFilePath + SWAP_INTENT_SUFFIX;
    if (!PlatformMetadata::readFileMetadata(intentFilePath).exists) {
        return;
    }
    std::ifstream intentFile(intentFilePath);
    if (!intentFile.is_open()) {
        reporter.writeWarning(
            string("A journal from a previous vacuum run is at ") + intentFilePath
            + ", but it could not be read. Look at it yourself before doing anything else."
        );
        return;
    }
    string originalPath;
    string displacedPath;
    string rewrittenPath;
    getline(intentFile, originalPath);
    getline(intentFile, displacedPath);
    getline(intentFile, rewrittenPath);
    // The stream is closed here rather than at the end of the function, because the
    // journal is removed below and Windows refuses to delete a file that is open.
    intentFile.close();
    const bool originalExists=PlatformMetadata::readFileMetadata(databaseFilePath).exists;
    const bool displacedExists=PlatformMetadata::readFileMetadata(displacedPath).exists;
    if (!originalExists && displacedExists) {
        // The interruption happened between moving the original aside and putting
        // the new file in place, so the original goes back where it belongs and the
        // half installed new file is discarded.
        std::error_code renameError;
        std::filesystem::rename(displacedPath, databaseFilePath, renameError);
        if (!renameError) {
            reporter.writeDetailLine(
                "A previous vacuum run was interrupted before the new file was in place, so the "
                "original file was put back."
            );
        }
    } else if (originalExists && displacedExists) {
        // The interruption happened after the new file was in place but before the
        // old one was cleared away. What is left over is the previous database.
        std::error_code removalError;
        std::filesystem::remove(displacedPath, removalError);
        reporter.writeDetailLine(
            "A previous vacuum run finished but did not clean up after itself; the leftover copy "
            "of the previous file was removed."
        );
    } else if (!originalExists && !displacedExists) {
        // Neither file is there, which a vacuum must never produce. The journal is
        // deliberately left in place so the paths stay on record.
        reporter.writeWarning(
            string("A previous vacuum run left neither the database nor its moved copy in place.\n\n"
                "The paths that run recorded were:\n  ") + originalPath + "\n  " + displacedPath
            + "\n\nLook for the safety copy the tool made before that run, and run the recover "
              "command before doing anything else."
        );
        return;
    }
    // Every other case ends with the swap resolved, so the journal and any
    // intermediate file can go.
    std::error_code cleanupError;
    if (!rewrittenPath.empty()) {
        std::filesystem::remove(rewrittenPath, cleanupError);
    }
    std::filesystem::remove(intentFilePath, cleanupError);
}
int VacuumCommand::readAutoVacuumMode(const string& databaseFilePath) {
    try {
        Database database(databaseFilePath, Database::OPEN_READ_ONLY);
        database.setBusyTimeoutMilliseconds(2000);
        const optional<long long> autoVacuumMode=database.queryPragmaInteger("PRAGMA auto_vacuum");
        return autoVacuumMode.has_value() ? static_cast<int>(autoVacuumMode.value()) : -1;
    } catch (const DatabaseOpenException&) {
        return -1;
    }
}
VacuumOutcome VacuumCommand::performIncrementalVacuum(
    const CommandOptions& options,
    const string& databaseFilePath
) {
    VacuumOutcome outcome;
    DestructiveOperationGuard guard(options, reporter);
    const FileMetadata sizeBefore=PlatformMetadata::readFileMetadata(databaseFilePath);
    outcome.sizeBeforeBytes=sizeBefore.logicalSizeBytes;
    outcome.peakFreeSpaceRequiredBytes=0;
    const long long freeBytes={StatisticsReader::reclaimableBytesFromFreeList(
        StatisticsReader::readHeaderFields(databaseFilePath)
    )};
    if (freeBytes<=0) {
        outcome.summarySentence="The file was already as small as it can be, so nothing was freed.";
        reporter.writeLine(outcome.summarySentence);
        return outcome;
    }
    const BackupRecord backup=guard.createBackup(databaseFilePath);
    if (!backup.wasCreated) {
        reporter.writeFailure(
            string("A safety copy could not be made, so nothing was changed."),
            string()
        );
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because a safety copy could not be made.";
        return outcome;
    }
    if (!guard.confirmWithUser(
        string("hand about ") + HumanReadableFormatting::formatBytes(freeBytes)
            + " of unused pages back to the drive. This database was set up for incremental "
              "auto-vacuum, so the pages are given back in place: no second file is written and no "
              "extra space is needed.",
        backup
    )) {
        outcome.summarySentence="Nothing was changed. Your database is exactly as it was.";
        return outcome;
    }
    {
        // The operation is SQLite's own: pages at the end of the file that hold
        // nothing are released and the file shrinks. It runs in one transaction per
        // page moved, which is why it is the cheap path.
        Database database(databaseFilePath, Database::OPEN_READ_WRITE);
        database.setBusyTimeoutMilliseconds(30000);
        database.execute("PRAGMA incremental_vacuum");
    }
    // The result is checked before it is trusted. The quick check reads the whole
    // file, which is why the rewrite path runs it too; on this path the file was
    // changed in place, so the check is the only evidence that the change was sound.
    bool fileIsSound=false;
    string quickCheckText;
    try {
        Database checkDatabase(databaseFilePath, Database::OPEN_READ_ONLY);
        Statement quickCheckStatement(
            checkDatabase.rawHandle(),
            "PRAGMA quick_check",
            "an integrity check of the vacuumed database"
        );
        if (quickCheckStatement.isValid() && quickCheckStatement.step()) {
            quickCheckText=quickCheckStatement.columnText(0);
            fileIsSound=quickCheckText=="ok";
            const int finalStepResultCode=quickCheckStatement.lastStepResultCode();
            if (finalStepResultCode!=SQLITE_DONE && finalStepResultCode!=SQLITE_ROW) {
                fileIsSound=false;
            }
        }
    } catch (const DatabaseOpenException&) {
        fileIsSound=false;
    }
    if (!fileIsSound) {
        // What SQLite actually said, so the failure can be acted on rather than only
        // known about. An empty string means the check produced no answer at all,
        // which is its own kind of failure.
        reporter.writeDetailLine(
            string("The integrity check reported: ")
            + (quickCheckText.empty() ? string("(no result)") : quickCheckText)
        );
        DestructiveOperationGuard restoringGuard(options, reporter);
        const bool restored=restoringGuard.restoreBackup(backup, databaseFilePath);
        reporter.writeFailure(
            string("The integrity check after the incremental vacuum did not pass. ")
            + (restored
                ? string("The safety copy was put back, so your database is exactly as it was.")
                : string("The safety copy could not be put back automatically; it is at ")
                    + backup.backupFilePath + "."),
            string()
        );
        outcome.wasRefused=true;
        outcome.wasRefusedForDamage=true;
        outcome.summarySentence="Nothing was changed, because the check after the vacuum did not pass.";
        return outcome;
    }
    const FileMetadata sizeAfter=PlatformMetadata::readFileMetadata(databaseFilePath);
    outcome.wasCompacted=true;
    outcome.usedIncrementalVacuum=true;
    outcome.sizeAfterBytes=sizeAfter.logicalSizeBytes;
    outcome.bytesReclaimed=outcome.sizeBeforeBytes-outcome.sizeAfterBytes;
    reporter.writeHeading("Done");
    reporter.writeLine("Size before: " + HumanReadableFormatting::formatBytes(outcome.sizeBeforeBytes));
    reporter.writeLine("Size after:  " + HumanReadableFormatting::formatBytes(outcome.sizeAfterBytes));
    reporter.writeLine(
        string("Given back to the drive: ") + HumanReadableFormatting::formatBytes(outcome.bytesReclaimed)
        + ". This database was set up for incremental auto-vacuum, so the pages were handed back in "
          "place: no second file was written and no extra space was needed."
    );
    const bool keepTheCopy=guard.shouldKeepBackup();
    guard.removeBackupAfterSuccess(backup);
    outcome.keptSafetyCopyBytes=keepTheCopy ? backup.copiedBytes : 0;
    outcome.netBytesFreed=outcome.bytesReclaimed-outcome.keptSafetyCopyBytes;
    outcome.summarySentence=string("Given back in place: ")
        + HumanReadableFormatting::formatBytes(outcome.bytesReclaimed)
        + " (incremental vacuum, no rewrite).";
    return outcome;
}
VacuumOutcome VacuumCommand::performVacuum(const CommandOptions& options, const string& databaseFilePath) {
    VacuumOutcome outcome;
    // The guard is built here so it reads the options of the run that is happening,
    // which is what makes the `--yes` flag reach the confirmation.
    DestructiveOperationGuard guard(options, reporter);
    // The auto-vacuum setting decides which of the two vacuum paths applies. A
    // database set up for incremental auto-vacuum can hand its pages back in place,
    // which needs no second file and no free space; every other one needs the
    // rewrite, which needs both.
    const int autoVacuumMode=readAutoVacuumMode(databaseFilePath);
    // The clock is read outside the block below, because that block closes the
    // connection and the readings have to outlive it.
    long long startNanoseconds=0;
    long long endNanoseconds=0;
    const FileMetadata sizeBefore=PlatformMetadata::readFileMetadata(databaseFilePath);
    outcome.sizeBeforeBytes=sizeBefore.logicalSizeBytes;
    // The space requirement is the whole size plus a margin, which is the worst
    // case: a database in which every page is still live would produce a new file of
    // exactly the same size.
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForCompaction(sizeBefore.logicalSizeBytes);
    outcome.peakFreeSpaceRequiredBytes=requirement.requiredBytes;
    const DiskSpace availableSpace=PlatformMetadata::readDiskSpace(databaseFilePath);
    if (options.dryRunOnly) {
        // A preview promises the file is untouched, so it stops here. The size,
        // the space the rewrite would need, and the space the free list says is
        // waiting inside the file are reported, and nothing is written.
        outcome.wasPreviewOnly=true;
        const long long reclaimableBytes=StatisticsReader::reclaimableBytesFromFreeList(
            StatisticsReader::readHeaderFields(databaseFilePath)
        );
        outcome.bytesReclaimed=reclaimableBytes;
        reporter.writeHeading("What the vacuum would do");
        reporter.writeLine("Unused space inside the file: "
            + HumanReadableFormatting::formatBytes(reclaimableBytes) + " would go back to the drive.");
        if (autoVacuumMode==2) {
            reporter.writeLine(
                "This database was set up for incremental auto-vacuum, so the pages would be handed "
                "back in place: no second file is written and no extra space is needed."
            );
        } else if (options.enableIncrementalAutoVacuum) {
            reporter.writeLine(
                "This run would also convert the database to incremental auto-vacuum, which is a "
                "persistent setting stored in the file: one full rewrite now, and later reclaims "
                "are cheap."
            );
        } else {
            reporter.writeLine(
                "This database is not set up for incremental auto-vacuum, so the pages are handed "
                "back by writing a smaller file beside this one and swapping it in. Pass "
                "--enable-auto-vacuum to convert it once, and later reclaims become cheap."
            );
        }
        outcome.summarySentence=string("Preview only: the vacuum would give back about ")
            + HumanReadableFormatting::formatBytes(reclaimableBytes) + ". Nothing was changed.";
        return outcome;
    }
    if (autoVacuumMode==2) {
        // The cheap path, and it comes before the space check on purpose: it needs no
        // second file, so "not enough free space" is not a reason to refuse it.
        return performIncrementalVacuum(options, databaseFilePath);
    }
    if (!DiskSpaceChecker::hasEnoughSpace(availableSpace, requirement)) {
        // The refusal is the important part of this command. Running out of room
        // half way through is how the story in the specification began.
        reporter.writeFailure(
            string("There is not enough free space to vacuum this database.\n\n")
            + "This drive has " + HumanReadableFormatting::formatBytes(availableSpace.availableBytes)
            + " free. The vacuum needs about " + HumanReadableFormatting::formatBytes(requirement.requiredBytes)
            + " while it works, which is about the size of the whole file, because it writes a new "
            + "file before it replaces the old one.\n\n"
            + "Nothing was changed. Free some space, or move the database to a drive with more room, "
            + "and run the command again. If the space is tight, the compact command frees space "
            + "inside the file first, which makes this one easier.",
            string()
        );
        outcome.wasRefusedForSpace=true;
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because there was not enough free space to do it safely.";
        return outcome;
    }
    // The new file is built under a name that cannot be mistaken for a database, so
    // an interrupted run leaves a file the tool can recognise and clean up rather
    // than one an agent might try to open.
    const string rewrittenFilePath=databaseFilePath + REWRITTEN_FILE_SUFFIX;
    {
        // A leftover intermediate file from an earlier run is removed first, and the
        // removal is checked. A stale file that survived would be mistaken for this
        // run's output below: its size and its integrity check would both pass, and
        // an out of date copy of the database would be swapped in as though the
        // vacuum had succeeded.
        std::error_code staleFileError;
        std::filesystem::remove(rewrittenFilePath, staleFileError);
        if (staleFileError) {
            reporter.writeFailure(
                string("A file left over from an earlier run is in the way and could not be "
                    "removed:\n  ") + rewrittenFilePath
                + "\n\nNothing was changed. Delete that file yourself and run the command again.",
                string()
            );
            outcome.wasRefused=true;
            outcome.summarySentence="Nothing was changed, because a leftover file from an earlier run could not be removed.";
            return outcome;
        }
    }
    // The source connection is opened read only, so the original cannot be changed
    // even in principle. `VACUUM INTO` needs a read connection to work from and a
    // separate path to write to.
    //
    startNanoseconds=PlatformMetadata::monotonicNanoseconds();
    // Everything that needs the original open lives inside this block, because the
    // handle has to be closed before the file can be renamed. On Windows a rename
    // fails while any process has the file open, and SQLite does not open its files
    // with delete sharing, so leaving this connection alive would make the swap
    // impossible.
    {
    // The one setting this command can change. SQLite applies a pending
    // auto-vacuum change to the file that VACUUM produces, so asking for the
    // conversion here means it lands in the new file, not in the original. The
    // source is opened for writing only when the user asked for the conversion.
    Database sourceDatabase(
        databaseFilePath,
        options.enableIncrementalAutoVacuum ? Database::OPEN_READ_WRITE : Database::OPEN_READ_ONLY
    );
    if (options.enableIncrementalAutoVacuum) {
        sourceDatabase.applyPragma("PRAGMA auto_vacuum = INCREMENTAL");
    }
    sourceDatabase.setBusyTimeoutMilliseconds(5000);
    // The pragma insists on a literal filename rather than a bound parameter, which
    // is a limitation of the statement rather than of this tool. The path is quoted
    // by doubling any single quote inside it, which is the rule for a SQL string
    // literal and is what makes putting the path in the text safe.
    const string quotedTargetPath=toSqlStringLiteral(rewrittenFilePath);
    Statement vacuumStatement(
        sourceDatabase.rawHandle(),
        string(VACUUM_INTO_PRAGMA) + " " + quotedTargetPath,
        "the rewrite that produces a smaller database"
    );
    if (!vacuumStatement.isValid()) {
        reporter.writeFailure(
            string("This version of SQLite in this build cannot perform the rewrite, so nothing "
                "was changed. The database is fine as it is; the only loss is the unused space "
                "inside it."),
            string()
        );
        outcome.wasRefusedForDamage=true;
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because the rewrite could not be started.";
        return outcome;
    }
    if (!vacuumStatement.run()) {
        // The rewrite itself failed. Its result is checked because a silent failure
        // followed by a leftover file would be indistinguishable from success below.
        const int rewriteResultCode=sourceDatabase.lastResultCode();
        reporter.writeFailure(
            string("The rewrite could not be completed, so nothing was changed. This usually means "
                "the drive ran out of room or the database is damaged. SQLite reported:\n  ")
            + sourceDatabase.lastErrorMessage() + "\n\nRun the diagnose command, and then the "
            "recover command, to see what it says.",
            to_string(rewriteResultCode)
        );
        {
            std::error_code cleanupError;
            std::filesystem::remove(rewrittenFilePath, cleanupError);
        }
        outcome.wasRefused=true;
        outcome.wasRefusedForDamage=true;
        outcome.summarySentence="Nothing was changed, because the rewrite could not be completed.";
        return outcome;
    }
    endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    const FileMetadata sizeAfter=PlatformMetadata::readFileMetadata(rewrittenFilePath);
    if (sizeAfter.logicalSizeBytes<=0) {
        // The rewrite produced nothing usable, so the file that is being built is
        // removed and the original is left exactly as it was.
        reporter.writeFailure(
            string("The rewrite did not produce a usable file, so the original was left exactly as "
                "it was and the incomplete file was deleted. Nothing was changed.\n\nThis file "
                "began as a SQLite database but the copy of it that was written could not be "
                "read back, which usually means the database is damaged. Run the diagnose "
                "command, and then the recover command, to see what it says."),
            string()
        );
        {
            std::error_code ignoredError;
            std::filesystem::remove(rewrittenFilePath, ignoredError);
        }
        outcome.wasRefusedForDamage=true;
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because the rewrite could not be completed.";
        return outcome;
    }
    // The new file is verified before it is trusted. A quick integrity check costs
    // one pass over the new file, which is worth it before the original is replaced.
    bool newFileIsSound=false;
    try {
        Database rewrittenDatabase(rewrittenFilePath, Database::OPEN_READ_ONLY);
        Statement quickCheckStatement(
            rewrittenDatabase.rawHandle(),
            QUICK_CHECK_PRAGMA,
            "an integrity check of the rewritten database"
        );
        if (quickCheckStatement.isValid() && quickCheckStatement.step()) {
            newFileIsSound=quickCheckStatement.columnText(0)=="ok";
        }
    } catch (const DatabaseOpenException&) {
        newFileIsSound=false;
    }
    if (!newFileIsSound) {
        reporter.writeFailure(
            string("The rewritten file did not pass its integrity check, so it was deleted and the "
                "original was left exactly as it was. Nothing was changed.\n\n"
                "This is what happens when the database is already damaged. Run the recover "
                "command for the safe next step."),
            string()
        );
        {
            std::error_code ignoredError;
            std::filesystem::remove(rewrittenFilePath, ignoredError);
        }
        outcome.wasRefusedForDamage=true;
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because the rewrite did not pass its own integrity check.";
        return outcome;
    }
    }
    // The safety copy is made before the swap. It is what makes the swap reversible.
    const BackupRecord backup=guard.createBackup(databaseFilePath);
    if (!backup.wasCreated) {
        reporter.writeFailure(
            string("A safety copy of the original could not be made, so the file was not replaced. "
                "The rewritten file has been deleted. Nothing was changed.\n\n"
                "Free some space and run the command again."),
            string()
        );
        {
            std::error_code ignoredError;
            std::filesystem::remove(rewrittenFilePath, ignoredError);
        }
        outcome.wasRefusedForSpace=true;
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because a safety copy could not be made.";
        return outcome;
    }
    if (!guard.confirmWithUser(
        "replace the database with a smaller copy of itself",
        backup
    )) {
        {
            std::error_code ignoredError;
            std::filesystem::remove(rewrittenFilePath, ignoredError);
        }
        outcome.summarySentence="Nothing was changed. Your database is exactly as it was.";
        return outcome;
    }
    // The swap. It happens in four steps: record the intent, move the original and
    // its side files aside, move the new file into place, and clear away what was
    // displaced. The intent journal makes a power loss between any two steps
    // resolvable on the next run, and the original is renamed rather than deleted,
    // so there is never a moment when neither file exists.
    const string displacedFilePath=databaseFilePath + DISPLACED_FILE_SUFFIX;
    if (!writeSwapIntent(databaseFilePath, displacedFilePath, rewrittenFilePath)) {
        std::error_code cleanupError;
        std::filesystem::remove(rewrittenFilePath, cleanupError);
        reporter.writeFailure(
            string("The journal that makes an interrupted swap recoverable could not be written "
                "beside the database, so nothing was changed. This usually means the folder is "
                "read only or the drive is full."),
            string()
        );
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because the swap could not be made recoverable.";
        return outcome;
    }
    // A leftover displaced file from an interrupted run is removed before the names
    // are reused, and then the original and its side files move as one group: the
    // side files belong to the original, and leaving them beside the new file is
    // the stale pairing that corrupts a database when a log is replayed against a
    // file it did not come from.
    std::error_code swapError;
    std::filesystem::remove(displacedFilePath, swapError);
    if (!swapError) {
        std::filesystem::rename(databaseFilePath, displacedFilePath, swapError);
    }
    if (!swapError) {
        for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
            const string sideFilePath=databaseFilePath + sideFileSuffix;
            if (!PlatformMetadata::readFileMetadata(sideFilePath).exists) {
                continue;
            }
            std::filesystem::rename(sideFilePath, displacedFilePath + sideFileSuffix, swapError);
            if (swapError) {
                break;
            }
        }
    }
    if (swapError) {
        // Something did not move. Everything that did move is put back, the journal
        // is cleared, and nothing has changed.
        std::error_code rollbackError;
        for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
            const string displacedSideFilePath=displacedFilePath + sideFileSuffix;
            if (PlatformMetadata::readFileMetadata(displacedSideFilePath).exists) {
                std::filesystem::rename(displacedSideFilePath, databaseFilePath + sideFileSuffix, rollbackError);
            }
        }
        std::filesystem::rename(displacedFilePath, databaseFilePath, rollbackError);
        std::filesystem::remove(rewrittenFilePath, rollbackError);
        clearSwapIntent(databaseFilePath);
        reporter.writeFailure(
            string("The original file could not be moved aside, so nothing was changed. This "
                "usually means the coding agent still has the database open, or the drive is "
                "read only."),
            string()
        );
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because the original file could not be moved aside.";
        return outcome;
    }
    std::filesystem::rename(rewrittenFilePath, databaseFilePath, swapError);
    if (swapError) {
        // The new file could not be put in place, so the original and its side files
        // are moved back. The database is left exactly as it was found.
        std::error_code rollbackError;
        for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
            const string displacedSideFilePath=displacedFilePath + sideFileSuffix;
            if (PlatformMetadata::readFileMetadata(displacedSideFilePath).exists) {
                std::filesystem::rename(displacedSideFilePath, databaseFilePath + sideFileSuffix, rollbackError);
            }
        }
        std::filesystem::rename(displacedFilePath, databaseFilePath, rollbackError);
        clearSwapIntent(databaseFilePath);
        reporter.writeFailure(
            string("The smaller file could not be moved into place, so the original was put back. "
                "Nothing was changed."),
            string()
        );
        outcome.wasRefused=true;
        outcome.summarySentence="Nothing was changed, because the smaller file could not be moved into place.";
        return outcome;
    }
    clearSwapIntent(databaseFilePath);
    // The displaced file and its side files are removed only after the new one is
    // confirmed in place. The safety copy made above is the copy the user is told
    // about; this one is the intermediate, and keeping it would waste the space the
    // user just recovered. A failure here costs space, not correctness, so it is
    // reported rather than treated as an error.
    std::error_code cleanupError;
    std::filesystem::remove(displacedFilePath, cleanupError);
    for (const char* sideFileSuffix : SIDE_FILE_SUFFIXES) {
        std::filesystem::remove(displacedFilePath + sideFileSuffix, cleanupError);
    }
    if (cleanupError) {
        reporter.writeWarning(
            string("The previous file could not be cleared away completely; remove it yourself to "
                "get that space back:\n  ") + displacedFilePath
        );
    }
    const FileMetadata finalSize=PlatformMetadata::readFileMetadata(databaseFilePath);
    outcome.wasCompacted=true;
    outcome.sizeAfterBytes=finalSize.logicalSizeBytes;
    outcome.bytesReclaimed=outcome.sizeBeforeBytes-outcome.sizeAfterBytes;
    reporter.writeHeading("Done");
    reporter.writeLine("Size before: " + HumanReadableFormatting::formatBytes(outcome.sizeBeforeBytes));
    reporter.writeLine("Size after:  " + HumanReadableFormatting::formatBytes(outcome.sizeAfterBytes));
    if (outcome.bytesReclaimed>0) {
        reporter.writeLine("Given back to the drive: "
            + HumanReadableFormatting::formatBytes(outcome.bytesReclaimed)
            + " (" + HumanReadableFormatting::formatPercentage(
                static_cast<double>(outcome.bytesReclaimed) / static_cast<double>(outcome.sizeBeforeBytes),
                1
            ) + " of the file)");
    } else {
        // A vacuum that frees nothing is worth saying out loud, because a user
        // who spent the disk space to run it deserves to know what happened.
        reporter.writeLine("Given back to the drive: nothing. Every page in this database is still in use, "
            "so the file was already as small as it can be.");
    }
    reporter.writeLine("The rewrite took "
        + HumanReadableFormatting::formatDuration(endNanoseconds - startNanoseconds) + ".");
    if (options.enableIncrementalAutoVacuum) {
        // The conversion is claimed only after the new file says so.
        const int newAutoVacuumMode=readAutoVacuumMode(databaseFilePath);
        if (newAutoVacuumMode==2) {
            reporter.writeLine(
                "This database is now set up for incremental auto-vacuum: one expensive rewrite "
                "has bought cheap reclaims from here on."
            );
        } else {
            reporter.writeWarning(
                "The request to convert this database to incremental auto-vacuum did not take "
                "effect in the new file. The rewrite itself succeeded; run the command again with "
                "the flag to try once more."
            );
        }
    }
    // The safety copy has done its job once the file is in place and checked. A kept
    // copy is space the drive did not get back, so the net change is reported
    // explicitly rather than hiding behind the file's new size.
    const bool keepTheCopy=guard.shouldKeepBackup();
    guard.removeBackupAfterSuccess(backup);
    outcome.keptSafetyCopyBytes=keepTheCopy ? backup.copiedBytes : 0;
    outcome.netBytesFreed=outcome.bytesReclaimed-outcome.keptSafetyCopyBytes;
    if (keepTheCopy) {
        const string signedNetChange=outcome.netBytesFreed>=0
            ? string("+") + HumanReadableFormatting::formatBytes(outcome.netBytesFreed)
            : string("-") + HumanReadableFormatting::formatBytes(-outcome.netBytesFreed);
        reporter.writeWarning(
            string("The safety copy was kept, and it occupies ")
            + HumanReadableFormatting::formatBytes(backup.copiedBytes)
            + ".\n\nSo the free space on the drive changed by " + signedNetChange
            + ": the file shrank by " + HumanReadableFormatting::formatBytes(outcome.bytesReclaimed)
            + " while the copy holds " + HumanReadableFormatting::formatBytes(backup.copiedBytes)
            + ".\n\nDelete the copy when you are satisfied with the result, and that space comes "
              "back too."
        );
    }
    outcome.summarySentence=outcome.bytesReclaimed>0
        ? HumanReadableFormatting::formatBytes(outcome.bytesReclaimed) + " was given back to the drive."
            + (keepTheCopy ? " The safety copy was kept, so the net change is "
                + HumanReadableFormatting::formatBytes(outcome.netBytesFreed>=0
                    ? outcome.netBytesFreed
                    : -outcome.netBytesFreed) + "." : string())
        : string("The file was already as small as it can be, so nothing was freed.");
    return outcome;
}
int VacuumCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
        writePurpose(options);
        if (options.verbose) {
            // The verbose flag shows the settings the run actually uses, so a user
            // reading a log can tell what produced the result.
            reporter.writeDetailLine(
                string("Safety copy: ")
                + (options.backupFilePath.empty() ? string("beside the database") : options.backupFilePath)
                + (options.keepBackup || !options.backupFilePath.empty()
                    ? ", kept after a successful run"
                    : ", removed after a successful run")
            );
            reporter.writeDetailLine(
                "The original is moved aside while the new file takes its place. The swap is "
                "journaled, so an interruption between any two steps is resolved on the next run."
            );
        }
    }
    if (options.databaseFilePath.empty()) {
        lastRunOutcome.summarySentence=string(
            "This command needs to know which database to work on, because it replaces one. "
            "Tell it with --database, or run the diagnose command first to see where your agent "
            "keeps it."
        );
        reporter.writeFailure(lastRunOutcome.summarySentence, string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    // A previous run may have been interrupted between two renames, and an
    // interruption between the first two leaves the database path empty. Resolving
    // that therefore comes before anything checks whether the file is there.
    resolveInterruptedVacuumSwap(options.databaseFilePath);
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    if (!fileStatus.exists) {
        lastRunOutcome.summarySentence=string("There is no file at ")
            + options.databaseFilePath + ". Nothing was changed.";
        reporter.writeFailure(lastRunOutcome.summarySentence, string());
        return 1;
    }
    if (fileStatus.isDirectory) {
        lastRunOutcome.summarySentence=string(
            "That path is a folder, not a database file, so there is nothing to vacuum.\n\n"
            "Several supported coding agents keep their history as plain text files in a "
            "folder rather than in a SQLite database. This tool does not change the files "
            "inside a folder.\n\nNothing was changed."
        );
        reporter.writeFailure(lastRunOutcome.summarySentence, string());
        return 1;
    }
    if (!StatisticsReader::readHeaderFields(options.databaseFilePath).isValidSqliteFile) {
        lastRunOutcome.summarySentence=string(
            "The file at ") + options.databaseFilePath + " is not a SQLite database, so there "
            "is nothing to vacuum.\n\nNothing was changed, and the file was not opened for "
            "writing. If you meant a different file, run the diagnose command to see where each "
            "supported agent keeps its database.";
        reporter.writeFailure(lastRunOutcome.summarySentence, string());
        return 1;
    }
    // The lock is checked before anything is written, because a database that is
    // open cannot be replaced safely.
    const SafetyCheckResult lockCheck=SafetyChecks::verifyDatabaseIsNotLocked(options.databaseFilePath);
    if (!lockCheck.passed()) {
        lastRunOutcome.summarySentence=lockCheck.explanation() + "\n\nNothing was changed.";
        reporter.writeFailure(lockCheck.explanation(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    // A previous run may have been interrupted between two renames. Resolving that
    // comes before anything else looks at the file, because the file it would look
    // at might be the wrong one.
    try {
        lastRunOutcome=performVacuum(options, options.databaseFilePath);
    } catch (const PragmaException& pragmaFailure) {
        // A setting could not be applied, which stops the run rather than continuing
        // with settings the caller did not ask for.
        lastRunOutcome.summarySentence=pragmaFailure.message() + "\n\nNothing was changed.";
        reporter.writeFailure(pragmaFailure.message(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const InsufficientDiskSpaceException& spaceFailure) {
        // There was not enough room to do what was asked, which is the refusal the
        // specification requires rather than a failure.
        lastRunOutcome.summarySentence=spaceFailure.message() + "\n\nNothing was changed.";
        reporter.writeFailure(spaceFailure.message(), to_string(spaceFailure.requiredBytes()));
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const FileSystemException& fileFailure) {
        lastRunOutcome.summarySentence=fileFailure.message() + "\n\nNothing was changed.";
        reporter.writeFailure(fileFailure.message(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const ConfigurationException& configurationFailure) {
        lastRunOutcome.summarySentence=configurationFailure.message();
        reporter.writeFailure(configurationFailure.message(), string());
        return 1;
    } catch (const AgentDatabaseMissingException& missingFailure) {
        // No supported agent could be found, which is a result rather than a fault.
        lastRunOutcome.summarySentence=missingFailure.message();
        reporter.writeFailure(missingFailure.message(), string());
        return 1;
    } catch (const DatabaseOpenException& openFailure) {
        lastRunOutcome.summarySentence=openFailure.message() + "\n\nNothing was changed.";
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const DatabaseOperationException& operationFailure) {
        // The original is replaced only by a finished, checked rewrite, so a stop
        // partway means the original is still exactly where it was.
        lastRunOutcome.summarySentence=operationFailure.message()
            + "\n\nThe original file is still in place, exactly as it was.";
        reporter.writeFailure(operationFailure.message(), string());
        return 1;
    }
    if (options.machineReadableOutput) {
        // The result object carries the typed numbers behind the sentences, and it
        // is emitted once the outcome is final, whatever the tail below decides.
        reporter.writeMachineResult("vacuum", vector<MachineField>{
            MachineField::boolean("previewOnly", lastRunOutcome.wasPreviewOnly),
            MachineField::boolean("refused", lastRunOutcome.wasRefused),
            MachineField::boolean("compacted", lastRunOutcome.wasCompacted),
            MachineField::boolean("usedIncrementalVacuum", lastRunOutcome.usedIncrementalVacuum),
            MachineField::number("sizeBeforeBytes", lastRunOutcome.sizeBeforeBytes),
            MachineField::number("sizeAfterBytes", lastRunOutcome.sizeAfterBytes),
            MachineField::number("bytesReclaimed", lastRunOutcome.bytesReclaimed),
            MachineField::number("keptSafetyCopyBytes", lastRunOutcome.keptSafetyCopyBytes),
            MachineField::number("netBytesFreed", lastRunOutcome.netBytesFreed)
        });
    }
    if (lastRunOutcome.wasPreviewOnly) {
        reporter.writeLine(lastRunOutcome.summarySentence);
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Run this command again without --dry-run to perform the vacuum.")
        });
        return 0;
    }
    if (lastRunOutcome.wasRefused) {
        // A refusal is not a success, so the caller's shell must be able to see it.
        reporter.writeLine(lastRunOutcome.summarySentence);
        return 1;
    }
    if (lastRunOutcome.wasCompacted) {
        vector<string> suggestedSteps;
        if (lastRunOutcome.keptSafetyCopyBytes>0) {
            suggestedSteps.push_back(
                string("Delete the safety copy when you are satisfied with the result: ")
                + "agent-db-doctor backups delete --database " + options.databaseFilePath
            );
        }
        suggestedSteps.push_back(string("Run the diagnose command to see the new size."));
        suggestedSteps.push_back(
            string("Run the service install command so unused space is returned automatically next time.")
        );
        reporter.writeSuggestedNextSteps(suggestedSteps);
        return 0;
    }
    reporter.writeLine(lastRunOutcome.summarySentence);
    return 0;
}
}
