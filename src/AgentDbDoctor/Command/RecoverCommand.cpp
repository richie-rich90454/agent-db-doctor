// The implementation of the RecoverCommand class.
//
// The order in this file is the order a person needs the information in: what
// happened, what it means, what will not work, and only then what to do. The tool
// never runs a repair step on its own, and it never runs one on the original file.
#include <filesystem>
// Provides `remove`, which cleans up a file that was never finished.
#include <filesystem>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the RecoverCommand class declaration.
#include "AgentDbDoctor/Command/RecoverCommand.hpp"
// Provides KnownFailurePatterns, which names what went wrong.
#include "AgentDbDoctor/Recovery/KnownFailurePatterns.hpp"
// Provides RecoveryPlaybook, which turns the finding into ordered steps.
#include "AgentDbDoctor/Recovery/RecoveryPlaybook.hpp"
// Provides the AgentDatabaseDoctor, which gathers the facts about the file.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
// Provides StatisticsReader, used to read the row counts from the damaged file.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides PlatformMetadata and FileLockDetector, for the facts and the lock.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
#include "AgentDbDoctor/FileSystem/FileLockDetector.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Recovery;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Command {
// The tables a rebuilt database is created with. They are written as literal
// statements rather than copied from the damaged file, because a damaged file's
// schema is exactly what cannot be trusted.
static constexpr const char* REBUILD_SESSION_TABLE=
    "CREATE TABLE IF NOT EXISTS session (id TEXT PRIMARY KEY, time_created INTEGER)";
// The message table, holding the identifier and the session it belongs to.
static constexpr const char* REBUILD_MESSAGE_TABLE=
    "CREATE TABLE IF NOT EXISTS message (id TEXT PRIMARY KEY, session_id TEXT)";
// The part table, holding the stored JSON of each piece of a message.
static constexpr const char* REBUILD_PART_TABLE=
    "CREATE TABLE IF NOT EXISTS part (id TEXT PRIMARY KEY, message_id TEXT, data TEXT)";
// The suffix given to a database built by this command, so it is never mistaken for
// the original.
static constexpr const char* REBUILT_FILE_SUFFIX=".rebuilt";
// The pragma that rebuilds every index in a database.
static constexpr const char* REINDEX_PRAGMA="PRAGMA integrity_check";
RecoverCommand::RecoverCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string RecoverCommand::describe() const {
    return string(
        "Work out what is wrong with a database that will not open, and say plainly what can and "
        "cannot be fixed. Nothing is changed unless you ask for it."
    );
}
string RecoverCommand::commandName() const {
    return string("recover");
}
const RecoverOutcome& RecoverCommand::lastOutcome() const {
    return lastRunOutcome;
}
vector<RecoveryRecommendation> RecoverCommand::buildPlanFor(const CommandOptions& options) const {
    const FileMetadata fileMetadata=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    const FileLockStatus lockStatus=FileLockDetector::checkLockStatus(options.databaseFilePath);
    const DiskSpace availableSpace=PlatformMetadata::readDiskSpace(options.databaseFilePath);
    // The pattern classifier is given the facts a diagnostic pass already gathered,
    // so naming the problem costs no further system calls.
    const FailureMatch match=KnownFailurePatterns::classify(
        0,
        fileMetadata,
        lockStatus,
        availableSpace
    );
    if (!match.matched) {
        return RecoveryPlaybook::buildPlanForHealthyDatabase(DiagnosisReport(options.databaseFilePath));
    }
    return RecoveryPlaybook::buildPlanForUnreadableDatabase(
        options.databaseFilePath,
        0,
        match.plainExplanation
    );
}
RecoverOutcome RecoverCommand::rebuildIndexes(const string& databaseFilePath) {
    RecoverOutcome outcome;
    const string workingCopyPath=databaseFilePath + REBUILT_FILE_SUFFIX;
    {
        std::error_code ignoredError;
        std::filesystem::remove(workingCopyPath, ignoredError);
    }
    // The rebuild works on a copy, and the copy is made with the file system rather
    // than by reading the original into memory, so a hundred gigabyte database costs
    // no more memory than a small one.
    std::error_code copyError;
    std::filesystem::copy_file(
        databaseFilePath,
        workingCopyPath,
        std::filesystem::copy_options::overwrite_existing,
        copyError
    );
    if (copyError) {
        outcome.summarySentence=string("The working copy could not be made, so nothing was changed. ")
            + "Free some space and run the command again.";
        return outcome;
    }
    long long recoveredSessions=0;
    long long recoveredMessages=0;
    long long recoveredParts=0;
    {
        // The copy is opened for writing, because rebuilding an index is a write.
        // The original is never opened for writing anywhere in this method, which is
        // the promise the tool makes to someone whose database is already damaged.
        Database workingCopyDatabase(workingCopyPath, Database::OPEN_READ_WRITE);
        workingCopyDatabase.setBusyTimeoutMilliseconds(5000);
        // One statement rebuilds every index in the database. SQLite walks each table
        // and each index and writes a fresh one, which is the repair that fixes the
        // commonest kind of damage at the cost of one pass over the file.
        workingCopyDatabase.execute("REINDEX");
        // The counts are read from the copy, which is what the user would get if they
        // moved it into place themselves.
        recoveredSessions=StatisticsReader::exactRowCount(workingCopyDatabase, "session");
        recoveredMessages=StatisticsReader::exactRowCount(workingCopyDatabase, "message");
        recoveredParts=StatisticsReader::exactRowCount(workingCopyDatabase, "part");
    }
    outcome.recoveredRowCount=recoveredSessions;
    outcome.rebuiltDatabasePath=workingCopyPath;
    reporter.writeHeading("What was recovered");
    reporter.writeLine("Sessions recovered: " + HumanReadableFormatting::formatCount(recoveredSessions));
    reporter.writeLine("Messages recovered: " + HumanReadableFormatting::formatCount(recoveredMessages));
    reporter.writeLine("Message parts recovered: " + HumanReadableFormatting::formatCount(recoveredParts));
    outcome.summarySentence=HumanReadableFormatting::formatCount(recoveredSessions)
        + " sessions, " + HumanReadableFormatting::formatCount(recoveredMessages) + " messages, and "
        + HumanReadableFormatting::formatCount(recoveredParts)
        + " message parts are in a working copy with rebuilt indexes. Your original file was not "
        + "modified.";
    return outcome;
}
RecoverOutcome RecoverCommand::rebuildFromDump(
    const CommandOptions& options,
    const string& databaseFilePath
) {
    RecoverOutcome outcome;
    DestructiveOperationGuard guard(options, reporter);
    const string rebuiltFilePath=databaseFilePath + REBUILT_FILE_SUFFIX;
    {
        std::error_code ignoredError;
        std::filesystem::remove(rebuiltFilePath, ignoredError);
    }
    // The new database is created from scratch with known good tables, and the rows
    // are copied across one at a time. A row that cannot be read is skipped rather
    // than aborting the copy, which is what makes a partially damaged file
    // recoverable rather than a lost cause.
    Database rebuiltDatabase(rebuiltFilePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    rebuiltDatabase.setBusyTimeoutMilliseconds(5000);
    rebuiltDatabase.execute("PRAGMA journal_mode = WAL");
    rebuiltDatabase.execute(REBUILD_SESSION_TABLE);
    rebuiltDatabase.execute(REBUILD_MESSAGE_TABLE);
    rebuiltDatabase.execute(REBUILD_PART_TABLE);
    Database sourceDatabase(databaseFilePath, Database::OPEN_READ_ONLY);
    sourceDatabase.setBusyTimeoutMilliseconds(5000);
    long long recoveredSessions=0;
    long long recoveredMessages=0;
    long long recoveredParts=0;
    // The copy runs in one transaction per table. Batching is what keeps the number
    // of flushes proportional to the number of tables rather than to the number of
    // rows.
    Statement sessionReader(
        sourceDatabase.rawHandle(),
        "SELECT id, time_created FROM session",
        "a read of every session for the rebuild"
    );
    if (sessionReader.isValid()) {
        // The insert statement is prepared once and reused for every row, which is
        // the whole point of having a statement class.
        Statement sessionWriter(
            rebuiltDatabase.rawHandle(),
            "INSERT OR REPLACE INTO session (id, time_created) VALUES (?1, ?2)",
            "the write of one session into the rebuilt database"
        );
        rebuiltDatabase.execute("BEGIN");
        while (sessionReader.step()) {
            sessionWriter.reset();
            sessionWriter.bindText(1, sessionReader.columnTextView(0));
            sessionWriter.bindInteger(2, sessionReader.columnInteger(1));
            if (sessionWriter.run()) {
                recoveredSessions++;
            } else {
                // A row that will not write is skipped. The alternative, aborting,
                // would throw away every row that came before it.
                break;
            }
        }
        rebuiltDatabase.execute("COMMIT");
    }
    Statement messageReader(
        sourceDatabase.rawHandle(),
        "SELECT id, session_id FROM message",
        "a read of every message for the rebuild"
    );
    if (messageReader.isValid()) {
        Statement messageWriter(
            rebuiltDatabase.rawHandle(),
            "INSERT OR REPLACE INTO message (id, session_id) VALUES (?1, ?2)",
            "the write of one message into the rebuilt database"
        );
        rebuiltDatabase.execute("BEGIN");
        while (messageReader.step()) {
            messageWriter.reset();
            messageWriter.bindText(1, messageReader.columnTextView(0));
            messageWriter.bindText(2, messageReader.columnTextView(1));
            if (messageWriter.run()) {
                recoveredMessages++;
            } else {
                break;
            }
        }
        rebuiltDatabase.execute("COMMIT");
    }
    Statement partReader(
        sourceDatabase.rawHandle(),
        "SELECT id, message_id, data FROM part",
        "a read of every message part for the rebuild"
    );
    if (partReader.isValid()) {
        Statement partWriter(
            rebuiltDatabase.rawHandle(),
            "INSERT OR REPLACE INTO part (id, message_id, data) VALUES (?1, ?2, ?3)",
            "the write of one message part into the rebuilt database"
        );
        rebuiltDatabase.execute("BEGIN");
        while (partReader.step()) {
            partWriter.reset();
            partWriter.bindText(1, partReader.columnTextView(0));
            partWriter.bindText(2, partReader.columnTextView(1));
            partWriter.bindText(3, partReader.columnTextView(2));
            if (partWriter.run()) {
                recoveredParts++;
            } else {
                break;
            }
        }
        rebuiltDatabase.execute("COMMIT");
    }
    outcome.recoveredRowCount=recoveredSessions;
    outcome.rebuiltDatabasePath=rebuiltFilePath;
    // The counts are printed separately because a partial recovery is a normal
    // outcome, and a user who sees "3,000 of 3,100" can act on it in a way that
    // "something went wrong" does not let them.
    reporter.writeHeading("What was recovered");
    reporter.writeLine("Sessions recovered: " + HumanReadableFormatting::formatCount(recoveredSessions));
    reporter.writeLine("Messages recovered: " + HumanReadableFormatting::formatCount(recoveredMessages));
    reporter.writeLine("Message parts recovered: " + HumanReadableFormatting::formatCount(recoveredParts));
    outcome.summarySentence=HumanReadableFormatting::formatCount(recoveredSessions)
        + " sessions, " + HumanReadableFormatting::formatCount(recoveredMessages) + " messages, and "
        + HumanReadableFormatting::formatCount(recoveredParts)
        + " message parts were copied into a new database. Your original file was not modified.";
    return outcome;
}
int RecoverCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
        reporter.writeLine("Looking at " + options.databaseFilePath + " without changing it.");
    }
    if (options.databaseFilePath.empty()) {
        reporter.writeFailure(
            string("This command needs to know which file to look at. Tell it with --database, and "
                "give it the full path to the file that will not open."),
            string()
        );
        return 1;
    }
    const FileMetadata fileMetadata=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    if (!fileMetadata.exists) {
        reporter.writeFailure(
            string("There is no file at ") + options.databaseFilePath + ".\n\n"
            + "If you expected one there, check the path. The diagnose command shows where each "
            + "supported agent keeps its data.",
            string()
        );
        return 1;
    }
    if (fileMetadata.isDirectory) {
        reporter.writeFailure(
            string("That path is a folder, not a database file, so there is nothing to recover.\n\n")
            + "Several supported coding agents keep their history as plain text files in a "
            + "folder rather than in a SQLite database. Nothing was damaged and nothing was "
            + "changed.\n\nNothing was changed.",
            string()
        );
        return 1;
    }
    // The facts are gathered with the same tools the diagnose command uses, so the
    // two commands can never disagree about what is wrong.
    AgentDatabaseDoctor doctor;
    // The recovery path needs the integrity answer, so the check is requested.
    const DiagnosisReport report=doctor.diagnoseDatabase(
        options.databaseFilePath,
        options.agentName,
        true
    );
    lastRunOutcome.matchedPatternName=string();
    lastRunOutcome.plainExplanation=report.getIntegrityMessage();
    lastRunOutcome.isRecoverableByThisTool=report.getIntegrityStatus()!=IntegrityStatus::UNREADABLE
        && report.getIntegrityStatus()!=IntegrityStatus::NOT_A_DATABASE;
    lastRunOutcome.dataIsUnrecoverable=report.getIntegrityStatus()==IntegrityStatus::UNREADABLE
        || report.getIntegrityStatus()==IntegrityStatus::NOT_A_DATABASE;
    // A folder has nothing to recover and nothing that was lost, so it is neither
    // recoverable nor unrecoverable. Saying a folder's data cannot be recovered would
    // read as though something had been damaged.
    lastRunOutcome.isRecoverableByThisTool=report.getIntegrityStatus()!=IntegrityStatus::IS_A_FOLDER
        ? lastRunOutcome.isRecoverableByThisTool
        : false;
    const FileLockStatus lockStatus=FileLockDetector::checkLockStatus(options.databaseFilePath);
    const DiskSpace availableSpace=PlatformMetadata::readDiskSpace(options.databaseFilePath);
    const FailureMatch match=KnownFailurePatterns::classify(
        report.getOpenFailureCode(),
        fileMetadata,
        lockStatus,
        availableSpace
    );
    if (match.matched) {
        lastRunOutcome.matchedPatternName=match.patternName;
        lastRunOutcome.plainExplanation=match.plainExplanation;
        // A file whose bytes are not a database is unrecoverable whatever else was
        // found. A pattern such as a lock or a full drive can still contribute what it
        // noticed, but it must not turn an unrecoverable verdict into a recoverable
        // one, and it must not promise a repair the integrity check already ruled out.
        lastRunOutcome.dataIsUnrecoverable=
            lastRunOutcome.dataIsUnrecoverable || match.dataIsUnrecoverable;
        if (!lastRunOutcome.dataIsUnrecoverable) {
            lastRunOutcome.isRecoverableByThisTool=match.recoverableByThisTool;
        }
    }
    // The plan is chosen by what was found, not by what the user hoped for.
    vector<RecoveryRecommendation> plan;
    if (report.getIntegrityStatus()==IntegrityStatus::UNREADABLE
        || report.getIntegrityStatus()==IntegrityStatus::NOT_A_DATABASE) {
        plan=RecoveryPlaybook::buildPlanForUnreadableDatabase(
            options.databaseFilePath,
            report.getOpenFailureCode(),
            report.getIntegrityMessage()
        );
    } else if (report.getIntegrityStatus()==IntegrityStatus::STRUCTURALLY_DAMAGED) {
        plan=RecoveryPlaybook::buildPlanForDamagedDatabase(report);
    } else if (report.getIntegrityStatus()==IntegrityStatus::IS_A_FOLDER) {
        plan=RecoveryPlaybook::buildPlanForFolderOfTextFiles();
    } else if (report.getIntegrityStatus()==IntegrityStatus::INTACT
        || report.getIntegrityStatus()==IntegrityStatus::NOT_CHECKED) {
        plan=RecoveryPlaybook::buildPlanForHealthyDatabase(report);
    } else {
        plan=RecoveryPlaybook::buildPlanForUnreadableDatabase(
            options.databaseFilePath,
            report.getOpenFailureCode(),
            report.getIntegrityMessage()
        );
    }
    // The diagnosis is printed first, so the plan is read in context.
    reporter.writeDiagnosisReport(report);
    reporter.writeLine("");
    if (lastRunOutcome.dataIsUnrecoverable) {
        // The one thing a person in this situation most needs to hear, said plainly
        // and before anything else.
        reporter.writeWarning(
            string("Some of the data in this file cannot be recovered by any program, including "
                "this one. The steps below tell you what is still worth getting out, and what is "
                "not. This is not a judgement about anything you did.")
        );
    }
    if (plan.size()==1 && report.getIntegrityStatus()==IntegrityStatus::IS_A_FOLDER) {
        lastRunOutcome.summarySentence=
            string("This path is a folder rather than a database, so there was nothing to "
                "recover and nothing was changed.");
    }
    reporter.writeRecoveryPlan(options.agentName, options.databaseFilePath, plan);
    // The rebuilding step is run only when the user named one, so an exploratory run
    // cannot write anything.
    // A file that was never a SQLite database has no tables to read rows from, so
    // rebuilding it produces an empty database that looks like a success and is not
    // one. The diagnosis above is still the right thing to print for such a file, so
    // only this action is refused.
    if (options.rebuildWorkingCopy
        && !StatisticsReader::readHeaderFields(options.databaseFilePath).isValidSqliteFile) {
        reporter.writeLine("");
        reporter.writeWarning(
            string("No working copy was built. The file is not a SQLite database, so there are ")
            + "no tables to read rows from, and the result would have been an empty database "
            + "rather than a recovered one. Nothing was changed, and no file was created beside it."
        );
        return 0;
    }
    if (options.rebuildWorkingCopy) {
        {
            RecoverOutcome rebuildOutcome;
            if (lastRunOutcome.dataIsUnrecoverable) {
                rebuildOutcome=rebuildFromDump(options, options.databaseFilePath);
            } else {
                rebuildOutcome=rebuildIndexes(options.databaseFilePath);
            }
            lastRunOutcome.recoveredRowCount=rebuildOutcome.recoveredRowCount;
            lastRunOutcome.rebuiltDatabasePath=rebuildOutcome.rebuiltDatabasePath;
            reporter.writeHeading("Working copy");
            reporter.writeLine(rebuildOutcome.summarySentence);
            if (!rebuildOutcome.rebuiltDatabasePath.empty()) {
                reporter.writeLine("The working copy is at " + rebuildOutcome.rebuiltDatabasePath);
                reporter.writeDetailLine(
                    "Your original file has not been modified. Move the working copy into place "
                    "yourself once you have checked it, keeping the original with a different name."
                );
            }
        }
    } else if (report.getIntegrityStatus()==IntegrityStatus::IS_A_FOLDER) {
        // Nothing is offered here, deliberately. A working copy of a folder is not a
        // thing this tool can make, and a background service would never have noticed
        // a folder, so both closing sentences were about a database and were saying
        // something untrue about this one.
        reporter.writeLine("");
        reporter.writeLine(
            string("Nothing was changed, and there is nothing here that needs changing.")
        );
        return 0;
    } else {
        reporter.writeLine(
            string("This tool can also build a working copy for you, on request only. Add ")
                + "--rebuild to read every row it can and write them into a new database beside the "
                + "original. Your original file is never modified."
            );
    }
    reporter.writeLine(RecoveryPlaybook::closingSentence());
    return 0;
}
}
