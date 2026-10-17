// The implementation of the CompactCommand class.
//
// The order of the steps below is the whole safety argument, and it is deliberate:
// prove the content is elsewhere, count the change, copy the file, sample the oldest
// sessions, ask permission, delete in batches, and then check the sample again. No
// step is skipped for speed, and every one of them can stop the operation before the
// next one begins. A preview run stops after the counting and writes nothing.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CompactCommand class declaration.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
// Provides PlatformMetadata, for the file sizes and the lock checks.
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
// Provides SafetyChecks, for the checks that decide whether removal is safe.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
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
// The query that counts the event rows that are known older copies, one event type
// at a time. The `type` column is indexed, so the count is a lookup rather than a
// scan. The predicate is spelled with `NOT EXISTS` against the materialised keep
// list, exactly as the deletion uses it, and it matches the row's `aggregate_id`,
// which is the session the event belongs to in the agent's own schema.
static constexpr const char* REDUNDANT_ROW_COUNT_QUERY=
    "SELECT COUNT(*) FROM event e WHERE e.type=?1 AND NOT EXISTS ("
    "  SELECT 1 FROM (SELECT id FROM session ORDER BY time_created DESC LIMIT ?2) k "
    "  WHERE k.id=e.aggregate_id)";
CompactCommand::CompactCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string CompactCommand::describe() const {
    return string(
        "Compact the event log: keep the newest snapshot of every message and remove the older "
        "ones. The conversation text is kept, a full copy of your database is made first, and the "
        "oldest sessions are checked afterwards to prove nothing was lost."
    );
}
string CompactCommand::commandName() const {
    return string("compact");
}
void CompactCommand::writePurpose(const CommandOptions& options) const {
    reporter.writeLine("Looking for redundant snapshots in the event log.");
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
const EventLogCompactionReport& CompactCommand::lastOutcome() const {
    return lastRunOutcome;
}
optional<EventLogCompactionPlan> CompactCommand::buildPlan(
    const CommandOptions& options,
    Database& database,
    EventLogCompactionReport& reportOut
) {
    // The first check is the one that matters most. If the message text lived only
    // in the rows about to be deleted, this run would destroy the conversation, so
    // the command stops here and says so.
    const SafetyCheckResult contentCheck=SafetyChecks::verifyMessageContentPersistsOutsideEventTable(database);
    if (!contentCheck.passed()) {
        reporter.writeFailure(contentCheck.explanation(), string());
        reportOut.summarySentence="Nothing was removed, because the tool could not prove the message text would survive.";
        return nullopt;
    }
    const SafetyCheckResult schemaCheck=SafetyChecks::verifySchemaSupportsCompaction(database);
    if (!schemaCheck.passed()) {
        reporter.writeFailure(schemaCheck.explanation(), string());
        reportOut.summarySentence="Nothing was removed, because this database is not laid out in a way the tool recognises.";
        return nullopt;
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
        reportOut.summarySentence="Nothing was removed, because the event rows belong to no generation the tool knows.";
        return nullopt;
    }
    // The number of sessions is counted once. It is a full scan of a small table,
    // and the answer decides the keep boundary.
    const long long totalSessionCount=StatisticsReader::exactRowCount(database, "session");
    if (totalSessionCount==0) {
        reportOut.summarySentence="There are no sessions in this database, so there was nothing to compact.";
        return nullopt;
    }
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
            + "than any real compaction needs. If you meant one part in twenty, write 0.05. If you "
            + "meant to keep a fixed number of conversations, use --keep instead.\n\nNothing was changed.",
            string()
        );
        reportOut.summarySentence="Nothing was removed, because the limit you gave was not a fraction this tool can use.";
        return nullopt;
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
    reportOut.protectedSessionCount=sessionsToKeep;
    EventLogCompactionPlan plan;
    plan.sessionsToKeep=sessionsToKeep;
    plan.totalSessionCount=totalSessionCount;
    // The event types that mark an older copy come from the agent's own
    // descriptor, which is the single place that names them.
    plan.redundantEventTypes=OpenCodeAgent().redundantEventTypes();
    // The rows the plan would remove are counted with one indexed lookup per type,
    // so the user sees the size of the change before agreeing to it.
    {
        Statement countingStatement(
            database.rawHandle(),
            REDUNDANT_ROW_COUNT_QUERY,
            "a count of the redundant snapshots in the event log"
        );
        for (const string& currentType : plan.redundantEventTypes) {
            if (!StatisticsReader::tableExists(database, "event")) {
                break;
            }
            countingStatement.reset();
            countingStatement.bindText(1, currentType);
            countingStatement.bindInteger(2, sessionsToKeep);
            if (countingStatement.step()) {
                plan.doomedRowCount+=countingStatement.columnInteger(0);
            }
        }
    }
    if (plan.doomedRowCount==0) {
        reportOut.summarySentence="There are no redundant snapshots in this event log, so nothing needed doing.";
        return nullopt;
    }
    // The refusal is arithmetic rather than a feeling. A change that would remove
    // more than half the rows usually means a misread count rather than a real
    // problem, so the tool stops and lets a human decide.
    const long long totalEventRows=StatisticsReader::approximateRowCount(database, "event")
        .value_or(0);
    if (totalEventRows>0) {
        const double fractionToRemove=static_cast<double>(plan.doomedRowCount) / static_cast<double>(totalEventRows);
        // A limit of zero is a real choice: it means remove nothing. Only a negative
        // value, which the command line cannot produce, means no choice was made.
        const double allowedFraction=options.maximumFractionToRemove>=0.0
            ? options.maximumFractionToRemove
            : DEFAULT_MAXIMUM_FRACTION_TO_REMOVE;
        if (fractionToRemove>allowedFraction) {
            reporter.writeFailure(
                string("This would remove ") + HumanReadableFormatting::formatCount(plan.doomedRowCount)
                + " of " + HumanReadableFormatting::formatCount(totalEventRows)
                + " rows, which is more than the "
                + HumanReadableFormatting::formatPercentage(allowedFraction, 0)
                + " limit this tool sets for itself. A number that large usually means something "
                + "is being miscounted, so nothing was changed.\n\n"
                + "If you are sure, raise the limit with --max-fraction, or name the number of "
                + "sessions to keep with --keep.",
                string()
            );
            reportOut.summarySentence="Nothing was removed, because the change would have been larger than this tool allows by itself.";
            return nullopt;
        }
    }
    return plan;
}
int CompactCommand::execute(const CommandOptions& options) {
    const bool previewOnly=options.dryRunOnly;
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
    if (fileStatus.isDirectory) {
        reporter.writeFailure(
            string("That path is a folder, not a database file, so there is nothing to compact.\n\n")
            + "Several supported coding agents keep their history as plain text files in a "
            + "folder rather than in a SQLite database. This tool does not change the files "
            + "inside a folder.\n\nNothing was changed.",
            string()
        );
        lastRunOutcome.summarySentence="Nothing was changed, because that path is a folder.";
        return 1;
    }
    if (!StatisticsReader::readHeaderFields(options.databaseFilePath).isValidSqliteFile) {
        reporter.writeFailure(
            string("The file at ") + options.databaseFilePath + " is not a SQLite database, so there is "
            + "nothing to compact.\n\nNothing was changed, and the file was not opened for "
            + "writing. If you meant a different file, run the diagnose command to see where "
            + "each supported agent keeps its database.",
            string()
        );
        lastRunOutcome.summarySentence=string("Nothing was changed, because there is no SQLite database at ")
            + options.databaseFilePath + ".";
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
        EventLogCompactionReport report;
        EventLogCompactionPlan plan;
        {
            // A preview reads the database and nothing else, so it opens read only.
            // A real run opens read and write, because its whole point is to change
            // something. The busy timeout stops it blocking forever behind a running
            // coding agent.
            Database database(
                options.databaseFilePath,
                previewOnly ? Database::OPEN_READ_ONLY : Database::OPEN_READ_WRITE
            );
            database.setBusyTimeoutMilliseconds(5000);
            if (!previewOnly) {
                // The three settings below are what make a large deletion fast.
                // Batched transactions are handled by the explicit `BEGIN` and
                // `COMMIT` in the loop; these two control how often the write ahead
                // log is folded back into the main file and how large it is allowed
                // to grow while that happens.
                database.applyPragma("PRAGMA wal_autocheckpoint = 0");
                database.applyPragma("PRAGMA journal_size_limit = 67108864");
                // Under a write ahead log, `NORMAL` means SQLite does not flush on
                // every commit. A crash still leaves a consistent database, because
                // the log is replayed, and the flush count drops from one per batch
                // to one per checkpoint.
                database.applyPragma("PRAGMA synchronous = NORMAL");
                // `temp_store = MEMORY` keeps the scratch b-trees SQLite builds while
                // running a large `DELETE` in memory rather than in a temporary file.
                // Measured on Windows, where every temporary file is scanned by
                // antivirus software as it is written, this alone cut the cost of
                // removing a row by about three times. It cannot make the run
                // unbounded, because the scratch structures grow with one batch of
                // rows rather than with the size of the table.
                database.applyPragma("PRAGMA temp_store = MEMORY");
                // A negative cache size is in kibibytes, so this is a sixty four
                // megabyte page cache. The default is about two megabytes, which is
                // far smaller than a large agent database, and the difference is
                // pages read from disk more than once. The cost is bounded and
                // known, rather than growing with the file.
                database.applyPragma("PRAGMA cache_size = -65536");
            }
            const optional<EventLogCompactionPlan> builtPlan=buildPlan(options, database, report);
            if (builtPlan.has_value()) {
                plan=builtPlan.value();
                reporter.writeHeading("What would be removed");
                reporter.writeLine("Redundant snapshots in the event log: "
                    + HumanReadableFormatting::formatCount(plan.doomedRowCount));
                reporter.writeLine("Sessions kept untouched:            "
                    + HumanReadableFormatting::formatCount(plan.sessionsToKeep)
                    + " of " + HumanReadableFormatting::formatCount(plan.totalSessionCount));
                EventLogCompactor compactor(reporter);
                report=compactor.run(
                    previewOnly ? CompactionMode::Preview : CompactionMode::Execute,
                    options,
                    database,
                    plan,
                    options.databaseFilePath
                );
            }
        }
        lastRunOutcome=report;
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
    if (lastRunOutcome.wasPreviewOnly) {
        reporter.writeLine(lastRunOutcome.summarySentence);
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Run this command again without --dry-run to perform the compaction."),
            string("Run the vacuum command after it. That is what gives the space back to the drive.")
        });
        return 0;
    }
    if (lastRunOutcome.anythingWasRemoved) {
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Run the vacuum command next. That is what actually gives the space back to the drive."),
            string("Run the service install command so this is caught early next time.")
        });
        return 0;
    }
    reporter.writeLine(lastRunOutcome.summarySentence);
    return 0;
}
}
