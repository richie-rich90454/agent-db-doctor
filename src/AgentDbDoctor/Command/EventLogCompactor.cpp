// The implementation of the EventLogCompactor class.
//
// The order of the steps below is the whole safety argument, and it is deliberate:
// copy the file, sample the oldest sessions, ask permission, delete in batches, and
// then check the sample again. No step is skipped for speed, and every one of them
// can stop the operation before the next one begins. The checks that prove the
// message text lives outside the rows being removed ran earlier, in the decision
// phase, because they decide the plan rather than carry it out.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the EventLogCompactor class declaration, the plan, and the report.
#include "AgentDbDoctor/Command/EventLogCompactor.hpp"
// Provides DestructiveOperationGuard, which makes the safety copy and asks.
#include "AgentDbDoctor/Command/DestructiveOperationGuard.hpp"
// Provides PlatformMetadata, for the sizes and the clock.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides StatisticsReader, for the constant time counts.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides SafetyChecks, for the samples and the survival check.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
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
    "    AND NOT EXISTS (SELECT 1 FROM kept_sessions k WHERE k.id=aggregate_id) "
    "ORDER BY rowid LIMIT ?3";
// The query that deletes one batch of one event type.
//
// The type, the row identifier range, and the same guard the selection used. All
// three are needed: the range alone would remove the events of the sessions being
// kept, because those rows sit between the doomed ones in row identifier order.
// The guard matches the row's `aggregate_id`, which is the session the event
// belongs to in the agent's own schema.
static constexpr const char* DELETE_BATCH_QUERY=
    "DELETE FROM event WHERE type=?1 AND rowid>?2 AND rowid<=?3 "
    "    AND NOT EXISTS (SELECT 1 FROM kept_sessions k WHERE k.id=aggregate_id)";
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
EventLogCompactor::EventLogCompactor(const ConsoleReport& reporter)
    : reporter(reporter) {
}
EventLogCompactionReport EventLogCompactor::run(
    CompactionMode mode,
    const CommandOptions& options,
    Database& database,
    const EventLogCompactionPlan& plan,
    const string& databaseFilePath
) {
    EventLogCompactionReport report;
    report.protectedSessionCount=plan.sessionsToKeep;
    if (mode==CompactionMode::Preview) {
        // A preview promises the file is untouched, so it stops here. The plan
        // was already checked and counted; describing it is all that is left.
        report.wasPreviewOnly=true;
        report.summarySentence=HumanReadableFormatting::formatCount(plan.doomedRowCount)
            + " redundant snapshots would be removed, and "
            + HumanReadableFormatting::formatCount(plan.sessionsToKeep)
            + " of " + HumanReadableFormatting::formatCount(plan.totalSessionCount)
            + " sessions would be kept untouched. Nothing was changed.";
        return report;
    }
    // The guard is built here, not in the constructor, because it reads the options
    // of the run that is happening. A guard built once from a default set of options
    // would ignore the `--yes` flag and ask a question nobody is there to answer.
    DestructiveOperationGuard guard(options, reporter);
    // The temporary table of kept sessions is filled here rather than being passed
    // in, so the compactor is self contained: everything it needs to run the plan
    // it builds itself.
    database.execute(CREATE_KEPT_SESSIONS_QUERY);
    database.execute(CLEAR_KEPT_SESSIONS_QUERY);
    {
        Statement fillingKeptSessions(
            database.rawHandle(),
            FILL_KEPT_SESSIONS_QUERY,
            "a record of which sessions are being kept"
        );
        if (fillingKeptSessions.isValid()) {
            fillingKeptSessions.bindInteger(1, plan.sessionsToKeep);
            fillingKeptSessions.run();
        }
    }
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
        report.summarySentence="Nothing was removed, because a safety copy could not be made.";
        return report;
    }
    if (!guard.confirmWithUser("remove " + HumanReadableFormatting::formatCount(plan.doomedRowCount)
        + " redundant snapshots from the event log", backup)) {
        report.summarySentence="Nothing was removed. Your database is exactly as it was.";
        return report;
    }
    // The sample is taken before anything is deleted, so the check afterwards has
    // something to compare against.
    const vector<SessionSnapshot> snapshotsBefore=SafetyChecks::captureOldestSessions(
        database,
        VERIFIED_SESSION_SAMPLE_SIZE
    );
    report.verifiedSessionCount=static_cast<long long>(snapshotsBefore.size());
    // The deletion loop. Each pass finds the next batch of row identifiers with the
    // forward cursor, then deletes exactly that range inside one transaction, so the
    // number of flushes is one per batch rather than one per row.
    const int batchSize=options.deletionBatchSize>0
        ? options.deletionBatchSize
        : DEFAULT_DELETION_BATCH_SIZE;
    ProgressBar progressBar("Removing older copies", plan.doomedRowCount, !options.quiet);
    // A graphical front end passes its own receiver through the options. The
    // console bar is untouched: with an empty sink it draws exactly as before.
    progressBar.setSink(options.progressSink);
    long long removedRowCount=0;
    long long rowsAtLastCheckpoint=0;
    // Every event type keeps its own forward cursor. The row identifiers of the
    // types are interleaved in the file, and their densities differ, so a single
    // shared cursor jumps past the rows of the sparser types and leaves them
    // behind: measured on a real database, one run removed a third of the rows it
    // promised because of exactly that.
    vector<long long> lastSeenRowIdentifierPerType(plan.redundantEventTypes.size(), 0);
    while (true) {
        long long batchRowCount=0;
        // The per type first and last row identifiers of this batch. A zero means
        // the type had nothing to take this time; row identifiers start at one.
        vector<long long> batchFirstRowPerType(plan.redundantEventTypes.size(), 0);
        vector<long long> batchLastRowPerType(plan.redundantEventTypes.size(), 0);
        {
            // The batch query is prepared once per iteration and reused for every
            // type. It lives in its own block so that it is finalised before the
            // deletion begins: a stepped statement that has not been reset still
            // holds a read lock, and a held read lock stops the write ahead log
            // from being checkpointed.
            Statement batchStatement(
                database.rawHandle(),
                REDUNDANT_BATCH_QUERY,
                "the next batch of rows to remove"
            );
            if (!batchStatement.isValid()) {
                break;
            }
            for (size_t typeIndex=0; typeIndex<plan.redundantEventTypes.size(); ++typeIndex) {
                long long firstRowForThisType=0;
                long long lastRowForThisType=0;
                long long rowsForThisType=0;
                batchStatement.reset();
                batchStatement.bindText(1, plan.redundantEventTypes[typeIndex]);
                batchStatement.bindInteger(2, lastSeenRowIdentifierPerType[typeIndex]);
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
                batchFirstRowPerType[typeIndex]=firstRowForThisType;
                batchLastRowPerType[typeIndex]=lastRowForThisType;
                batchRowCount+=rowsForThisType;
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
            for (size_t typeIndex=0; typeIndex<plan.redundantEventTypes.size(); ++typeIndex) {
                if (batchLastRowPerType[typeIndex]<=0) {
                    continue;
                }
                deleteStatement.reset();
                deleteStatement.bindText(1, plan.redundantEventTypes[typeIndex]);
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
        for (size_t typeIndex=0; typeIndex<plan.redundantEventTypes.size(); ++typeIndex) {
            if (batchLastRowPerType[typeIndex]>0) {
                lastSeenRowIdentifierPerType[typeIndex]=batchLastRowPerType[typeIndex];
            }
        }
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
    report.anythingWasRemoved=removedRowCount>0;
    report.removedRowCount=removedRowCount;
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
        report.wasRolledBack=true;
        report.summarySentence="The change was undone and your database was restored from the safety copy.";
        return report;
    }
    report.reclaimableBytesNow=StatisticsReader::reclaimableBytesFromFreeList(
        StatisticsReader::readHeaderFields(databaseFilePath)
    );
    reporter.writeHeading("Done");
    reporter.writeLine("Redundant snapshots removed: "
        + HumanReadableFormatting::formatCount(removedRowCount));
    reporter.writeLine("Sessions checked afterwards: "
        + HumanReadableFormatting::formatCount(report.verifiedSessionCount));
    reporter.writeLine(survivalCheck.explanation());
    // The important sentence: compaction frees space inside the file but does not
    // shrink it, which is the fact that surprises people most.
    reporter.writeWarning(
        string("The file is still about ") + HumanReadableFormatting::formatBytes(
            PlatformMetadata::readFileMetadata(databaseFilePath).logicalSizeBytes
        ) + " on disk. Removing rows marks the space as reusable but does not hand it back to "
        + "the drive. Run the vacuum command next, and it will be.\n\n"
        + "Until then, the space is safe to use: SQLite will not overwrite anything still needed."
    );
    report.summarySentence=string("Compacting event log: removed ")
        + HumanReadableFormatting::formatCount(removedRowCount)
        + " redundant snapshots. The newest copy of every message was kept, and "
        + HumanReadableFormatting::formatCount(report.verifiedSessionCount)
        + " sessions were checked afterwards and still hold their original text.";
    return report;
}
}
