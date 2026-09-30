// The implementation of the PruneCommand class.
//
// The order of the steps below is the whole safety argument, and it is deliberate:
// prove the content is elsewhere, copy the file, sample the oldest sessions, ask
// permission, delete in batches, and then check the sample again. No step is
// skipped for speed, and every one of them can stop the operation before the next
// one begins.
#include <string>
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
namespace AgentDbDoctor::Command {
// The query that finds the boundary between the sessions to keep and the ones to
// prune. Ordering by the creation time and taking one row is an indexed read, so it
// costs the same in a database of ten sessions and a database of a million.
static constexpr const char* SESSION_BOUNDARY_QUERY=
    "SELECT id FROM session ORDER BY time_created DESC LIMIT 1 OFFSET ?1";
// The query that counts the event rows that are known duplicates, one event type at
// a time. The `type` column is indexed, so the count is a lookup rather than a scan.
static constexpr const char* REDUNDANT_ROW_COUNT_QUERY=
    "SELECT COUNT(*) FROM event WHERE type=?1 AND rowid IN ("
    "  SELECT e.rowid FROM event e "
    "  WHERE e.type=?1 AND e.session_id NOT IN ("
    "    SELECT id FROM session ORDER BY time_created DESC LIMIT ?2))";
// The query that returns the row identifiers of one batch of duplicates, ordered so
// the next batch can start where this one finished.
//
// This is the forward cursor pattern the specification requires. A query using
// `OFFSET` would make SQLite walk past every row it skipped, which turns a sequence
// of batches into quadratic work. Asking for `rowid` greater than the last one seen
// uses the primary b-tree directly and costs a logarithmic step per batch.
static constexpr const char* REDUNDANT_BATCH_QUERY=
    "SELECT rowid FROM event "
    "WHERE type=?1 AND rowid>?2 AND session_id NOT IN ("
    "  SELECT id FROM session ORDER BY time_created DESC LIMIT ?3) "
    "ORDER BY rowid LIMIT ?4";
// The query that deletes one batch by its row identifiers.
static constexpr const char* DELETE_BATCH_QUERY="DELETE FROM event WHERE rowid>?1 AND rowid<=?2";
// How many checkpoints are allowed to be skipped between explicit ones. A
// checkpoint copies the write ahead log into the main file, and doing it inside a
// transaction makes an individual batch much slower.
static constexpr int BATCHES_PER_CHECKPOINT=50;
// The number of databases the command is prepared to handle in one run.
static constexpr int MAXIMUM_REPORTED_DATABASES=200;
PruneCommand::PruneCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached),
      guard(CommandOptions(), reporter) {
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
    // The connection is opened for reading and writing, because the point of the
    // command is to change something. The busy timeout stops it blocking forever
    // behind a running coding agent.
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
    // The event types that mark a duplicate come from the agent, not from a guess.
    const vector<string> redundantTypes=vector<string>{
        string("message.updated.1"),
        string("message.part.updated.1"),
        string("session.updated.1")
    };
    // The duplicate rows are counted first, with one indexed lookup per type, so the
    // user sees the size of the change before agreeing to it.
    Statement countingStatement(
        database.rawHandle(),
        REDUNDANT_ROW_COUNT_QUERY,
        "a count of the rows that are older copies of changed messages"
    );
    long long doomedRowCount=0;
    for (const string& currentType : redundantTypes) {
        if (!StatisticsReader::tableExists(database, "event")) {
            break;
        }
        countingStatement.reset();
        countingStatement.bindText(1, currentType);
        countingStatement.bindInteger(2, sessionsToKeep);
        if (countingStatement.step()) {
            doomedRowCount+=countingStatement.columnInteger(0);
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
        const double allowedFraction=options.maximumFractionToRemove>0.0
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
    long long lastSeenRowIdentifier=0;
    long long removedRowCount=0;
    int batchesSinceCheckpoint=0;
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
        long long batchFirstRow=0;
        long long batchLastRow=0;
        long long batchRowCount=0;
        for (const string& currentType : redundantTypes) {
            batchStatement.reset();
            batchStatement.bindText(1, currentType);
            batchStatement.bindInteger(2, lastSeenRowIdentifier);
            batchStatement.bindInteger(3, sessionsToKeep);
            batchStatement.bindInteger(4, batchSize);
            while (batchStatement.step()) {
                const long long currentRowIdentifier=batchStatement.columnInteger(0);
                if (batchRowCount==0) {
                    batchFirstRow=currentRowIdentifier;
                }
                batchLastRow=currentRowIdentifier;
                batchRowCount++;
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
            deleteStatement.bindInteger(1, batchFirstRow - 1);
            deleteStatement.bindInteger(2, batchLastRow);
            if (deleteStatement.step()) {
                const long long deletedInBatch=database.lastChangedRowCount();
                if (deletedInBatch>0) {
                    removedRowCount+=deletedInBatch;
                }
            }
        }
        database.execute("COMMIT");
        lastSeenRowIdentifier=batchLastRow;
        progressBar.advance(batchRowCount);
        batchesSinceCheckpoint++;
        if (batchesSinceCheckpoint>=BATCHES_PER_CHECKPOINT) {
            // The explicit checkpoint folds the write ahead log back into the main
            // file and keeps the log from growing without bound.
            database.execute("PRAGMA wal_checkpoint(TRUNCATE)");
            batchesSinceCheckpoint=0;
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
        reporter.writeFailure(
            string("This command needs to know which database to work on, because it changes one. "
                "Tell it with --database, or run the diagnose command first to see where your "
                "agent keeps it."),
            string()
        );
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    if (!fileStatus.exists) {
        reporter.writeFailure(
            string("There is no file at ") + options.databaseFilePath
            + ". Nothing was changed.",
            string()
        );
        return 1;
    }
    // The lock is checked before the copy, because copying a file that another
    // program is writing produces a copy that may not be complete, and a safety
    // copy that is not complete is worse than none.
    const SafetyCheckResult lockCheck=SafetyChecks::verifyDatabaseIsNotLocked(options.databaseFilePath);
    if (!lockCheck.passed()) {
        reporter.writeFailure(lockCheck.explanation(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    }
    try {
        lastRunOutcome=performPrune(options, options.databaseFilePath);
    } catch (const DatabaseOpenException& openFailure) {
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const DatabaseOperationException& operationFailure) {
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
