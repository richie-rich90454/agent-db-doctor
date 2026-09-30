// The implementation of the CompactCommand class.
//
// The safety properties this file maintains are: the original file is never opened
// for writing, the new file is built beside it and only swapped in once it has been
// verified, the free space needed is stated before the run starts, and no file
// system compression is used at any point.
#include <filesystem>
// Provides `rename` and `remove`, which swap the new file into place and clear away
// the old one.
#include <filesystem>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CompactCommand class declaration.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
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
static constexpr const char* REWRITTEN_FILE_SUFFIX=".compacting";
// The quick check pragma, run against the new file before it is swapped in.
static constexpr const char* QUICK_CHECK_PRAGMA="PRAGMA quick_check";
CompactCommand::CompactCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string CompactCommand::describe() const {
    return string(
        "Rewrite the database so it only occupies the space it actually needs. The original is "
        "never modified in place: a new file is built beside it, checked, and only then swapped in."
    );
}
string CompactCommand::commandName() const {
    return string("compact");
}
const CompactOutcome& CompactCommand::lastOutcome() const {
    return lastRunOutcome;
}
void CompactCommand::writePurpose(const CommandOptions& options) const {
    reporter.writeLine("Rewriting the database so it gives unused space back to the drive.");
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
CompactOutcome CompactCommand::performCompaction(const CommandOptions& options, const string& databaseFilePath) {
    CompactOutcome outcome;
    // The guard is built here so it reads the options of the run that is happening,
    // which is what makes the `--yes` flag reach the confirmation.
    DestructiveOperationGuard guard(options, reporter);
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
    if (!DiskSpaceChecker::hasEnoughSpace(availableSpace, requirement)) {
        // The refusal is the important part of this command. Running out of room
        // half way through is how the story in the specification began.
        reporter.writeFailure(
            string("There is not enough free space to compact this database.\n\n")
            + "This drive has " + HumanReadableFormatting::formatBytes(availableSpace.availableBytes)
            + " free. Compaction needs about " + HumanReadableFormatting::formatBytes(requirement.requiredBytes)
            + " while it works, which is about the size of the whole file, because it writes a new "
            + "file before it replaces the old one.\n\n"
            + "Nothing was changed. Free some space, or move the database to a drive with more room, "
            + "and run the command again. If the space is tight, the prune command frees space "
            + "inside the file first, which makes this one easier.",
            string()
        );
        outcome.wasRefusedForSpace=true;
        outcome.summarySentence="Nothing was changed, because there was not enough free space to do it safely.";
        return outcome;
    }
    // The new file is built under a name that cannot be mistaken for a database, so
    // an interrupted run leaves a file the tool can recognise and clean up rather
    // than one an agent might try to open.
    const string rewrittenFilePath=databaseFilePath + REWRITTEN_FILE_SUFFIX;
    {
        std::error_code ignoredError;
        std::filesystem::remove(rewrittenFilePath, ignoredError);
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
    Database sourceDatabase(databaseFilePath, Database::OPEN_READ_ONLY);
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
        outcome.summarySentence="Nothing was changed, because the rewrite could not be started.";
        return outcome;
    }
    vacuumStatement.step();
    endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    const FileMetadata sizeAfter=PlatformMetadata::readFileMetadata(rewrittenFilePath);
    if (sizeAfter.logicalSizeBytes<=0) {
        // The rewrite produced nothing usable, so the file that is being built is
        // removed and the original is left exactly as it was.
        reporter.writeFailure(
            string("The rewrite did not produce a usable file, so the original was left exactly as "
                "it was and the incomplete file was deleted. Nothing was changed.\n\nThis usually "
                "means the database is damaged. Run the diagnose command to see what it says."),
            string()
        );
        {
            std::error_code ignoredError;
            std::filesystem::remove(rewrittenFilePath, ignoredError);
        }
        outcome.wasRefusedForDamage=true;
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
    // The swap. The original is renamed rather than deleted, so there is never a
    // moment when neither file exists. If the rename of the new file into place were
    // to fail, the original would still be there under its old name.
    std::error_code ignoredError;
    const string displacedFilePath=databaseFilePath + ".before-compaction";
    std::filesystem::remove(displacedFilePath, ignoredError);
    std::filesystem::rename(databaseFilePath, displacedFilePath, ignoredError);
    if (ignoredError) {
        // The original did not move, so the new file is removed and nothing has
        // changed. The safety copy is still a valid second copy either way.
        std::filesystem::remove(rewrittenFilePath, ignoredError);
        reporter.writeFailure(
            string("The original file could not be moved aside, so nothing was changed. This "
                "usually means the coding agent still has the database open, or the drive is "
                "read only."),
            string()
        );
        outcome.summarySentence="Nothing was changed, because the original file could not be moved aside.";
        return outcome;
    }
    std::filesystem::rename(rewrittenFilePath, databaseFilePath, ignoredError);
    if (ignoredError) {
        // The new file could not be put in place, so the original is put back. The
        // database is left exactly as it was found.
        std::error_code rollbackError;
        std::filesystem::rename(displacedFilePath, databaseFilePath, rollbackError);
        reporter.writeFailure(
            string("The smaller file could not be moved into place, so the original was put back. "
                "Nothing was changed."),
            string()
        );
        outcome.summarySentence="Nothing was changed, because the smaller file could not be moved into place.";
        return outcome;
    }
    // The displaced file is removed only after the new one is confirmed in place.
    // The safety copy made above is the copy the user is told about; this one is the
    // intermediate, and keeping it would waste the space the user just recovered.
    std::filesystem::remove(displacedFilePath, ignoredError);
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
        // A compaction that frees nothing is worth saying out loud, because a user
        // who spent the disk space to run it deserves to know what happened.
        reporter.writeLine("Given back to the drive: nothing. Every page in this database is still in use, "
            "so the file was already as small as it can be.");
    }
    reporter.writeLine("The rewrite took "
        + HumanReadableFormatting::formatDuration(endNanoseconds - startNanoseconds) + ".");
    outcome.summarySentence=outcome.bytesReclaimed>0
        ? HumanReadableFormatting::formatBytes(outcome.bytesReclaimed) + " was given back to the drive."
        : string("The file was already as small as it can be, so nothing was freed.");
    return outcome;
}
int CompactCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
        writePurpose(options);
    }
    if (options.databaseFilePath.empty()) {
        reporter.writeFailure(
            string("This command needs to know which database to work on, because it replaces one. "
                "Tell it with --database, or run the diagnose command first to see where your agent "
                "keeps it."),
            string()
        );
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    if (!fileStatus.exists) {
        reporter.writeFailure(
            string("There is no file at ") + options.databaseFilePath + ". Nothing was changed.",
            string()
        );
        return 1;
    }
    // The lock is checked before anything is written, because a database that is
    // open cannot be replaced safely.
    const SafetyCheckResult lockCheck=SafetyChecks::verifyDatabaseIsNotLocked(options.databaseFilePath);
    if (!lockCheck.passed()) {
        reporter.writeFailure(lockCheck.explanation(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    try {
        lastRunOutcome=performCompaction(options, options.databaseFilePath);
    } catch (const PragmaException& pragmaFailure) {
        // A setting could not be applied, which stops the run rather than continuing
        // with settings the caller did not ask for.
        reporter.writeFailure(pragmaFailure.message(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const InsufficientDiskSpaceException& spaceFailure) {
        // There was not enough room to do what was asked, which is the refusal the
        // specification requires rather than a failure.
        reporter.writeFailure(spaceFailure.message(), to_string(spaceFailure.requiredBytes()));
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const FileSystemException& fileFailure) {
        reporter.writeFailure(fileFailure.message(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const ConfigurationException& configurationFailure) {
        reporter.writeFailure(configurationFailure.message(), string());
        return 1;
    } catch (const AgentDatabaseMissingException& missingFailure) {
        // No supported agent could be found, which is a result rather than a fault.
        reporter.writeFailure(missingFailure.message(), string());
        return 1;
    } catch (const DatabaseOpenException& openFailure) {
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const DatabaseOperationException& operationFailure) {
        reporter.writeFailure(operationFailure.message(), string());
        return 1;
    }
    if (lastRunOutcome.wasCompacted) {
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Run the diagnose command to see the new size."),
            string("Run the service install command so unused space is returned automatically next time.")
        });
        return 0;
    }
    reporter.writeLine(lastRunOutcome.summarySentence);
    return 0;
}
}
