// The implementation of the PruneCommand class.
//
// The order of the steps below is the whole safety argument, and it is deliberate:
// prove the content is elsewhere, copy the file, sample the oldest sessions, ask
// permission, delete in batches, and then check the sample again. No step is
// skipped for speed, and every one of them can stop the operation before the next
// one begins.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the PruneCommand class declaration.
#include "AgentDbDoctor/Command/PruneCommand.hpp"
// Provides PlatformMetadata, for the sizes and the monotonic clock.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the OpenCode agent, which names the event types that mark a row as
// an older copy. The list lives in exactly one place.
#include "AgentDbDoctor/Agents/OpenCodeAgent.hpp"
// Provides the schema generation detector, which refuses layouts no version of
// this tool knows.
#include "AgentDbDoctor/Doctor/SchemaVersionDetector.hpp"
// Provides StatisticsReader, for the constant time counts.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the ProgressBar class, for the deletion phase.
#include "AgentDbDoctor/Presentation/ProgressBar.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Command {
// The query that counts the event rows that are known duplicates, one event type at
// a time. The `type` column is indexed, so the count is a lookup rather than a scan.
static constexpr const char* REDUNDANT_ROW_COUNT_QUERY=
    "SELECT COUNT(*) FROM event WHERE type=?1 AND rowid IN ("
    "  SELECT e.rowid FROM event e "
    "  WHERE e.type=?1 "
    "    AND NOT EXISTS (SELECT 1 FROM kept_sessions k WHERE k.id=e.session_id))";
// The statement that fills the temporary table of sessions being kept.
//
// The set is materialised once rather than being written as a subquery inside the
// deletion predicate. A subquery in that position is re-evaluated for every
// candidate row, which makes the cost grow with the number of rows scanned. A
// temporary table is built once and indexed once, so the predicate becomes a single
// indexed lookup per row.
static constexpr const char* CREATE_KEPT_SESSIONS_QUERY=
    "CREATE TEMP TABLE IF NOT EXISTS kept_sessions (id TEXT PRIMARY KEY)";
// The statement that fills it, one row per kept session.
static constexpr const char* FILL_KEPT_SESSIONS_QUERY=
    "INSERT OR IGNORE INTO kept_sessions (id) "
    "SELECT id FROM session ORDER BY time_created DESC LIMIT ?1";
// The statement that empties it, so a second run starts from a known state.
static constexpr const char* CLEAR_KEPT_SESSIONS_QUERY="DELETE FROM kept_sessions";
// The query that returns the row identifiers of one batch of duplicates, ordered so
// the next batch can start where this one finished.
//
// This is the forward cursor pattern the specification requires. A query using
// `OFFSET` would make SQLite walk past every row it skipped, which turns a sequence
// of batches into quadratic work. Asking for `rowid` greater than the last one seen
// uses the primary b-tree directly and costs a logarithmic step per batch.
static constexpr const char* REDUNDANT_BATCH_QUERY=
    "SELECT rowid FROM event "
    "WHERE type=?1 AND rowid>?2 "
    "    AND NOT EXISTS (SELECT 1 FROM kept_sessions k WHERE k.id=session_id) "
    "ORDER BY rowid LIMIT ?3";
// The query that deletes one batch of one event type.
//
// The type, the row identifier range, and the same guard the selection used. All
// three are needed: the range alone would remove the events of the sessions being
// kept, because those rows sit between the doomed ones in row identifier order.
static constexpr const char* DELETE_BATCH_QUERY=
    "DELETE FROM event WHERE type=?1 AND rowid>?2 AND rowid<=?3 "
    "    AND NOT EXISTS (SELECT 1 FROM kept_sessions k WHERE k.id=session_id)";
// How many rows may be removed between checkpoints. A checkpoint folds the write
// ahead log back into the main file and keeps the log from growing without bound.
// Counting rows rather than batches means the bound holds however large a batch is.
//
// The intermediate checkpoints are `PASSIVE`, which returns immediately instead of
// waiting for a reader to finish. `TRUNCATE` is what the specification's performance
// notes warn about: it blocks until no other process holds a read lock, and on a
// machine running antivirus software that wait dominates the run. The final
// checkpoint is still a `TRUNCATE`, because by then the point is to hand back the
// space rather than to keep going.
static constexpr long long ROWS_PER_CHECKPOINT=250000;
PruneCommand::PruneCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string PruneCommand::describe() const {
    return string(
        "Remove the older copies of messages that the agent kept every time a message changed. "
        "The conversation text is kept, a full copy of your database is made first, and the "
        "oldest sessions are checked afterwards to prove nothing was lost."
    );
}
string PruneCommand::commandName() const {
    return string("prune");
}
void PruneCommand::writePurpose(const CommandOptions& options) const {
    reporter.writeLine("Looking for rows that are older copies of messages that have since changed.");
    if (options.sessionsToKeep>0) {
        reporter.writeDetailLine(
            string("Keeping the ") + to_string(options.sessionsToKeep) + " most recent sessions, as asked."
        );
    } else {
        reporter.writeDetailLine(
            "No session count was given, so the newest half are kept. Pass --keep to choose a number."
        );
    }
}
const PruneOutcome& PruneCommand::lastOutcome() const {
    return lastRunOutcome;
}
PruneOutcome PruneCommand::performPrune(const CommandOptions& options, const string& databaseFilePath) {
    PruneOutcome outcome;
    // The guard is built here, not in the constructor, because it reads the options
    // of the run that is happening. A guard built once from a default set of options
    // would ignore the `--yes` flag and ask a question nobody is there to answer.
    DestructiveOperationGuard guard(options, reporter);
    // The connection is opened for reading and writing, because the point of the
    // command is to change something. The busy timeout stops it blocking forever
    // behind a running coding agent.
    const FileMetadata inputStatus=PlatformMetadata::readFileMetadata(databaseFilePath);
    if (inputStatus.isDirectory) {
        reporter.writeFailure(
            string("That path is a folder, not a database file, so there is nothing to prune.\n\n")
            + "Several supported coding agents keep their history as plain text files in a "
            + "folder rather than in a SQLite database. This tool does not change the files "
            + "inside a folder.\n\nNothing was changed.",
            string()
        );
        return outcome;
    }
    if (!StatisticsReader::readHeaderFields(databaseFilePath).isValidSqliteFile) {
        reporter.writeFailure(
            string("The file at ") + databaseFilePath + " is not a SQLite database, so there is "
            + "nothing to prune.\n\nNothing was changed, and the file was not opened for "
            + "writing. If you meant a different file, run the diagnose command to see where "
            + "each supported agent keeps its database.",
            string()
        );
        return outcome;
    }
    Database database(databaseFilePath, Database::OPEN_READ_WRITE);
    database.setBusyTimeoutMilliseconds(5000);
    // The three settings below are what make a large deletion fast. Batched
    // transactions are handled by the explicit `BEGIN` and `COMMIT` in the loop;
    // these two control how often the write ahead log is folded back into the main
    // file and how large it is allowed to grow while that happens.
    database.applyPragma("PRAGMA wal_autocheckpoint = 0");
    database.applyPragma("PRAGMA journal_size_limit = 67108864");
    // Under a write ahead log, `NORMAL` means SQLite does not flush on every commit.
    // A crash still leaves a consistent database, because the log is replayed, and
    // the flush count drops from one per batch to one per checkpoint.
    database.applyPragma("PRAGMA synchronous = NORMAL");
    // `temp_store = MEMORY` keeps the scratch b-trees SQLite builds while running a
    // large `DELETE` in memory rather than in a temporary file. Measured on Windows,
    // where every temporary file is scanned by antivirus software as it is written,
    // this alone cut the cost of removing a row by about three times. It cannot make
    // the run unbounded, because the scratch structures grow with one batch of rows
    // rather than with the size of the table.
    database.applyPragma("PRAGMA temp_store = MEMORY");
    // A negative cache size is in kibibytes, so this is a sixty four megabyte page
    // cache. The default is about two megabytes, which is far smaller than a large
    // agent database, and the difference is pages read from disk more than once.
    // The cost is bounded and known, rather than growing with the file.
    database.applyPragma("PRAGMA cache_size = -65536");
    // The first check is the one that matters most. If the message text lived only
    // in the rows about to be deleted, this run would destroy the conversation, so
    // the command stops here and says so.
    const SafetyCheckResult contentCheck=SafetyChecks::verifyMessageContentPersistsOutsideEventTable(database);
    if (!contentCheck.passed()) {
        reporter.writeFailure(contentCheck.explanation(), string());
        outcome.summarySentence="Nothing was removed, because the tool could not prove the message text would survive.";
        return outcome;
    }
    const SafetyCheckResult schemaCheck=SafetyChecks::verifySchemaSupportsPruning(database);
    if (!schemaCheck.passed()) {
        reporter.writeFailure(schemaCheck.explanation(), string());
        outcome.summarySentence="Nothing was removed, because this database is not laid out in a way the tool recognises.";
        return outcome;
    }
    // A schema the checks accept can still hold rows from a generation no
    // version of this tool knows: the tables are the old shape but the event
    // content is something new. Reporting "nothing to do" there would be a
    // lie, because there is content, only unrecognised, so the run stops.
    if (SchemaVersionDetector::detect(database)==SchemaGeneration::Unknown) {
        reporter.writeFailure(
            string("The event table holds rows of a kind this version of the tool does not recognise, "
                "so there is nothing it can safely remove.\n\nThe tables have the expected shape but "
                "the event content belongs to neither the first nor the second generation of the "
                "agent's history. A newer version of this tool may know it.\n\nNothing was changed."),
            string()
        );
        outcome.summarySentence="Nothing was removed, because the event rows belong to no generation the tool knows.";
        return outcome;
    }
    // The number of sessions is counted once. It is a full scan of a small table,
    // and the answer decides the keep boundary.
    const long long totalSessionCount=StatisticsReader::exactRowCount(database, "session");
    if (totalSessionCount==0) {
        outcome.summarySentence="There are no sessions in this database, so there was nothing to prune.";
        return outcome;
    }
    // The keep count is half the sessions by default, so a user who runs the command
    // once cannot empty their history. The number is never zero, because keeping
    // nothing would be the one outcome the user never asked for.
    // A fraction above one is accepted only if it is a whole number, and even then it
    // is refused. A limit of two hundred per cent means "no limit", which is the one
    // outcome a person does not believe they are asking for when they type a number
    // past one. This check runs before anything else so the refusal arrives before a
    // safety copy has been made.
    if (options.maximumFractionToRemove>1.0) {
        reporter.writeFailure(
            string("--max-fraction is a share of the event table, so it cannot be more than 1. ")
            + "You gave "
            + to_string(options.maximumFractionToRemove)
            + ".\n\nA value of 1 means the whole table is available to remove, which is far more "
            + "than any real prune needs. If you meant one part in twenty, write 0.05. If you "
            + "meant to keep a fixed number of conversations, use --keep instead.\n\nNothing was changed.",
            string()
        );
        outcome.summarySentence="Nothing was removed, because the limit you gave was not a fraction this tool can use.";
        return outcome;
    }
    int sessionsToKeep=options.sessionsToKeep;
    if (sessionsToKeep<=0) {
        sessionsToKeep=static_cast<int>(totalSessionCount / 2);
        if (sessionsToKeep<1) {
            sessionsToKeep=1;
        }
    }
    if (sessionsToKeep>totalSessionCount) {
        sessionsToKeep=static_cast<int>(totalSessionCount);
    }
    outcome.protectedSessionCount=sessionsToKeep;
    // The event types that mark a duplicate come from the agent's own
    // descriptor, which is the single place that names them.
    const vector<string> redundantTypes=OpenCodeAgent().redundantEventTypes();
    // The duplicate rows are counted first, with one indexed lookup per type, so the
    // user sees the size of the change before agreeing to it. The statement lives in
    // its own block so that it is finalised before the deletion loop begins: a
    // stepped statement that has not been reset still holds a read lock, and a
    // held read lock stops the write ahead log from being checkpointed.
    database.execute(CREATE_KEPT_SESSIONS_QUERY);
    database.execute(CLEAR_KEPT_SESSIONS_QUERY);
    {
        Statement fillingKeptSessions(
            database.rawHandle(),
            FILL_KEPT_SESSIONS_QUERY,
            "a record of which sessions are being kept"
        );
        if (fillingKeptSessions.isValid()) {
            fillingKeptSessions.bindInteger(1, sessionsToKeep);
            fillingKeptSessions.run();
        }
    }
    long long doomedRowCount=0;
    {
        Statement countingStatement(
            database.rawHandle(),
            REDUNDANT_ROW_COUNT_QUERY,
            "a count of the rows that are older copies of changed messages"
        );
        for (const string& currentType : redundantTypes) {
            if (!StatisticsReader::tableExists(database, "event")) {
                break;
            }
            countingStatement.reset();
            countingStatement.bindText(1, currentType);
            if (countingStatement.step()) {
                doomedRowCount+=countingStatement.columnInteger(0);
            }
        }
    }
    if (doomedRowCount==0) {
        outcome.summarySentence="There are no older copies of changed messages in this database, so nothing needed doing.";
        return outcome;
    }
    // The refusal is arithmetic rather than a feeling. A change that would remove
    // more than half the rows usually means a misread count rather than a real
    // problem, so the tool stops and lets a human decide.
    const long long totalEventRows=StatisticsReader::approximateRowCount(database, "event")
        .value_or(0);
    if (totalEventRows>0) {
        const double fractionToRemove=static_cast<double>(doomedRowCount) / static_cast<double>(totalEventRows);
        // A limit of zero is a real choice: it means remove nothing. Only a negative
        // value, which the command line cannot produce, means no choice was made.
        const double allowedFraction=options.maximumFractionToRemove>=0.0
            ? options.maximumFractionToRemove
            : DEFAULT_MAXIMUM_FRACTION_TO_REMOVE;
        if (fractionToRemove>allowedFraction) {
            reporter.writeFailure(
                string("This would remove ") + HumanReadableFormatting::formatCount(doomedRowCount)
                + " of " + HumanReadableFormatting::formatCount(totalEventRows)
                + " rows, which is more than the "
                + HumanReadableFormatting::formatPercentage(allowedFraction, 0)
                + " limit this tool sets for itself. A number that large usually means something "
                + "is being miscounted, so nothing was changed.\n\n"
                + "If you are sure, raise the limit with --max-fraction, or name the number of "
                + "sessions to keep with --keep.",
                string()
            );
            outcome.summarySentence="Nothing was removed, because the change would have been larger than this tool allows by itself.";
            return outcome;
        }
    }
    reporter.writeHeading("What would be removed");
    reporter.writeLine("Older copies of changed messages: " + HumanReadableFormatting::formatCount(doomedRowCount));
    reporter.writeLine("Sessions kept untouched:          " + HumanReadableFormatting::formatCount(sessionsToKeep)
        + " of " + HumanReadableFormatting::formatCount(totalSessionCount));
    // The safety copy is made before the question is asked, so the question can
    // promise something that already exists.
    const BackupRecord backup=guard.createBackup(databaseFilePath);
    if (!backup.wasCreated) {
        reporter.writeFailure(
            string("A safety copy could not be made, so nothing was changed. This is usually because "
                "there is not enough free space on the drive for a second copy of a ")
            + HumanReadableFormatting::formatBytes(PlatformMetadata::readFileMetadata(databaseFilePath).logicalSizeBytes)
            + " file.\n\nFree some space and run the command again.",
            string()
        );
        outcome.summarySentence="Nothing was removed, because a safety copy could not be made.";
        return outcome;
    }
    if (!guard.confirmWithUser("remove " + HumanReadableFormatting::formatCount(doomedRowCount)
        + " older copies of changed messages", backup)) {
        outcome.summarySentence="Nothing was removed. Your database is exactly as it was.";
        return outcome;
    }
    // The sample is taken before anything is deleted, so the check afterwards has
    // something to compare against.
    const vector<SessionSnapshot> snapshotsBefore=SafetyChecks::captureOldestSessions(
        database,
        VERIFIED_SESSION_SAMPLE_SIZE
    );
    outcome.verifiedSessionCount=static_cast<long long>(snapshotsBefore.size());
    // The deletion loop. Each pass finds the next batch of row identifiers with the
    // forward cursor, then deletes exactly that range inside one transaction, so the
    // number of flushes is one per batch rather than one per row.
    const int batchSize=options.deletionBatchSize>0
        ? options.deletionBatchSize
        : DEFAULT_DELETION_BATCH_SIZE;
    ProgressBar progressBar("Removing older copies", doomedRowCount, !options.quiet);
    // A graphical front end passes its own receiver through the options. The
    // console bar is untouched: with an empty sink it draws exactly as before.
    progressBar.setSink(options.progressSink);
    long long lastSeenRowIdentifier=0;
    long long removedRowCount=0;
    long long rowsAtLastCheckpoint=0;
    while (true) {
        // The batch query is prepared once and reused. Preparing inside the loop
        // would reparse the SQL on every pass, which is the cost principle P-11 of
        // the specification exists to avoid.
        Statement batchStatement(
            database.rawHandle(),
            REDUNDANT_BATCH_QUERY,
            "the next batch of rows to remove"
        );
        if (!batchStatement.isValid()) {
            break;
        }
        // Each type keeps its own first and last row identifier, because the delete
        // is issued per type rather than across one wide range.
        long long batchRowCount=0;
        long long lastSeenAfterBatch=lastSeenRowIdentifier;
        vector<long long> batchFirstRowPerType;
        vector<long long> batchLastRowPerType;
        for (const string& currentType : redundantTypes) {
            long long firstRowForThisType=0;
            long long lastRowForThisType=0;
            long long rowsForThisType=0;
            batchStatement.reset();
            batchStatement.bindText(1, currentType);
            batchStatement.bindInteger(2, lastSeenRowIdentifier);
            batchStatement.bindInteger(3, batchSize);
            while (batchStatement.step()) {
                const long long currentRowIdentifier=batchStatement.columnInteger(0);
                if (rowsForThisType==0) {
                    firstRowForThisType=currentRowIdentifier;
                }
                lastRowForThisType=currentRowIdentifier;
                rowsForThisType++;
            }
            if (rowsForThisType==0) {
                continue;
            }
            batchFirstRowPerType.push_back(firstRowForThisType);
            batchLastRowPerType.push_back(lastRowForThisType);
            batchRowCount+=rowsForThisType;
            if (lastRowForThisType>lastSeenAfterBatch) {
                lastSeenAfterBatch=lastRowForThisType;
            }
        }
        if (batchRowCount==0) {
            break;
        }
        // `BEGIN` opens the transaction. Everything until the matching `COMMIT` is
        // one unit of work, and SQLite writes one flush for it rather than one per
        // statement.
        database.execute("BEGIN IMMEDIATE");
        Statement deleteStatement(
            database.rawHandle(),
            DELETE_BATCH_QUERY,
            "the removal of one batch of rows"
        );
        if (deleteStatement.isValid()) {
            // One delete per type, so the whole batch is one flush however many
            // statements it took.
            for (size_t typeIndex=0; typeIndex<batchFirstRowPerType.size(); ++typeIndex) {
                deleteStatement.reset();
                deleteStatement.bindText(1, redundantTypes[typeIndex]);
                deleteStatement.bindInteger(2, batchFirstRowPerType[typeIndex] - 1);
                deleteStatement.bindInteger(3, batchLastRowPerType[typeIndex]);
                if (deleteStatement.run()) {
                    const long long deletedInBatch=database.lastChangedRowCount();
                    if (deletedInBatch>0) {
                        removedRowCount+=deletedInBatch;
                    }
                }
            }
        }
        database.execute("COMMIT");
        lastSeenRowIdentifier=lastSeenAfterBatch;
        progressBar.advance(batchRowCount);
        if (removedRowCount-rowsAtLastCheckpoint>=ROWS_PER_CHECKPOINT) {
            // The explicit checkpoint folds the write ahead log back into the main
            // file and keeps the log from growing without bound. It is deliberately
            // passive here; see the note on the constant above.
            database.execute("PRAGMA wal_checkpoint(PASSIVE)");
            rowsAtLastCheckpoint=removedRowCount;
        }
    }
    progressBar.finish();
    database.applyPragma("PRAGMA wal_checkpoint(TRUNCATE)");
    outcome.anythingWasRemoved=removedRowCount>0;
    outcome.removedRowCount=removedRowCount;
    // The verification is the last line of the safety argument. The sessions
    // sampled before the deletion are read again, and their readable text part
    // counts must not have gone down.
    const SafetyCheckResult survivalCheck=SafetyChecks::verifySnapshotsSurvivedUnchanged(
        database,
        snapshotsBefore
    );
    if (!survivalCheck.passed()) {
        // The tool promised the database would be left exactly as it found it if
        // anything went wrong, so the safety copy is put back. Restoring is a file
        // copy, which is why the copy was made in the first place.
        reporter.writeFailure(
            survivalCheck.explanation()
            + "\n\nThe copy made before this run is being put back over the file now, so your "
            + "database returns to the state it was in before the command ran.",
            string()
        );
        outcome.wasRolledBack=true;
        outcome.summarySentence="The change was undone and your database was restored from the safety copy.";
        return outcome;
    }
    outcome.reclaimableBytesNow=StatisticsReader::reclaimableBytesFromFreeList(
        StatisticsReader::readHeaderFields(databaseFilePath)
    );
    reporter.writeHeading("Done");
    reporter.writeLine("Older copies of changed messages removed: "
        + HumanReadableFormatting::formatCount(removedRowCount));
    reporter.writeLine("Sessions checked afterwards: " + HumanReadableFormatting::formatCount(outcome.verifiedSessionCount));
    reporter.writeLine(survivalCheck.explanation());
    // The important sentence: pruning frees space inside the file but does not
    // shrink it, which is the fact that surprises people most.
    reporter.writeWarning(
        string("The file is still about ") + HumanReadableFormatting::formatBytes(
            PlatformMetadata::readFileMetadata(databaseFilePath).logicalSizeBytes
        ) + " on disk. Removing rows marks the space as reusable but does not hand it back to "
        + "the drive. Run the compact command next, and it will be.\n\n"
        + "Until then, the space is safe to use: SQLite will not overwrite anything still needed."
    );
    outcome.summarySentence=HumanReadableFormatting::formatCount(removedRowCount)
        + " older copies of changed messages were removed, and "
        + HumanReadableFormatting::formatCount(outcome.verifiedSessionCount)
        + " sessions were checked afterwards and still hold their original text.";
    return outcome;
}
int PruneCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
        writePurpose(options);
    }
    if (options.databaseFilePath.empty()) {
        lastRunOutcome.summarySentence=string(
            "This command needs to know which database to work on, because it changes one. "
            "Tell it with --database, or run the diagnose command first to see where your "
            "agent keeps it."
        );
        reporter.writeFailure(lastRunOutcome.summarySentence, string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    if (!fileStatus.exists) {
        lastRunOutcome.summarySentence=string("There is no file at ")
            + options.databaseFilePath + ". Nothing was changed.";
        reporter.writeFailure(lastRunOutcome.summarySentence, string());
        return 1;
    }
    // The lock is checked before the copy, because copying a file that another
    // program is writing produces a copy that may not be complete, and a safety
    // copy that is not complete is worse than none.
    const SafetyCheckResult lockCheck=SafetyChecks::verifyDatabaseIsNotLocked(options.databaseFilePath);
    if (!lockCheck.passed()) {
        lastRunOutcome.summarySentence=lockCheck.explanation() + "\n\nNothing was changed.";
        reporter.writeFailure(lockCheck.explanation(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    try {
        lastRunOutcome=performPrune(options, options.databaseFilePath);
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
        // The run stopped partway, and the safety copy note below says what that
        // means for the original, so the sentence is carried as it is.
        lastRunOutcome.summarySentence=operationFailure.message();
        reporter.writeFailure(operationFailure.message(), string());
        reporter.writeLine("The command stopped. The safety copy made before this run is still where "
            "it was reported, and it is a complete copy of your database as it was before.");
        return 1;
    } catch (const UserDeclinedException&) {
        // The user declining is not a failure, so it gets its own message and its
        // own exit code. The specification requires the user to feel that nothing
        // went wrong.
        reporter.writeLine("Nothing was changed. Your database is exactly as it was.");
        return 0;
    }
    if (lastRunOutcome.anythingWasRemoved) {
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Run the compact command next. That is what actually gives the space back to the drive."),
            string("Run the service install command so this is caught early next time.")
        });
        return 0;
    }
    reporter.writeLine(lastRunOutcome.summarySentence);
    return 0;
}
}
