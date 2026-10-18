// The implementation of the SessionsCommand class.
//
// The listing measures each session by the payload it stores, which reads the part
// table and is therefore a command a user asks for rather than something diagnose
// pays for. The deletion reuses the safety machinery the compaction uses: the guard
// for the copy and the question, the sampled sessions for the check afterwards, and
// the same rule that a refused or failed run puts the copy back.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides `cin` and `getline`, for the typed confirmation.
#include <iostream>
// Provides the `tolower` used by the confirmation comparison.
#include <cctype>
// Provides the SessionsCommand class declaration.
#include "AgentDbDoctor/Command/SessionsCommand.hpp"
// Provides the guard that makes the copy and asks.
#include "AgentDbDoctor/Command/DestructiveOperationGuard.hpp"
// Provides the space attribution's session ranking.
#include "AgentDbDoctor/Doctor/SpaceAttribution.hpp"
// Provides the sampled sessions used for the check after the deletion.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
// Provides StatisticsReader, for the column and table checks.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the Database wrapper.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
namespace {
// How many sessions the listing shows when the caller does not say.
constexpr int DEFAULT_SESSION_LIST_LIMIT=10;
// The sentence used when a deletion is refused because the newer tables are in use.
string secondGenerationRefusal() {
    return string(
        "This database has the agent's second generation tables in use, and their layout is not "
        "documented anywhere this tool trusts. Deleting a session whose newer rows live in those "
        "tables would leave the agent pointing at something that is half gone, so nothing was "
        "changed.\n\nThe compact and vacuum commands are safe here: they do not delete sessions."
    );
}
}
SessionsCommand::SessionsCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string SessionsCommand::describe() const {
    return string(
        "List the largest conversations by stored payload, or delete one after copying the database "
        "and checking that every other conversation survived."
    );
}
string SessionsCommand::commandName() const {
    return string("sessions");
}
int SessionsCommand::listSessions(const CommandOptions& options) {
    if (options.databaseFilePath.empty()) {
        reporter.writeFailure(
            string("Listing sessions needs to know which database to read. Tell it with --database."),
            string()
        );
        return 2;
    }
    try {
        Database database(options.databaseFilePath, Database::OPEN_READ_ONLY);
        database.setBusyTimeoutMilliseconds(5000);
        const int limit=options.sessionsToRank>0 ? options.sessionsToRank : DEFAULT_SESSION_LIST_LIMIT;
        const vector<SessionSizeRow> sessionRows=SpaceAttribution::largestSessions(database, limit);
        if (sessionRows.empty()) {
            reporter.writeLine("No sessions with stored parts were found in that database.");
            return 0;
        }
        if (reporter.isMachineReadable()) {
            string jsonText="{\"event\":\"sessions\",\"entries\":[";
            for (size_t sessionIndex=0; sessionIndex<sessionRows.size(); ++sessionIndex) {
                if (sessionIndex>0) {
                    jsonText+=",";
                }
                jsonText+="{\"id\":\"" + ConsoleReport::escapeForJson(sessionRows[sessionIndex].sessionIdentifier)
                    + "\",\"payloadBytes\":" + to_string(sessionRows[sessionIndex].payloadBytes)
                    + ",\"partCount\":" + to_string(sessionRows[sessionIndex].partCount) + "}";
            }
            jsonText+="]}";
            reporter.writeMachineObject(jsonText);
            reporter.writeMachineResult("sessions", vector<MachineField>{
                MachineField::number("listedSessions", static_cast<long long>(sessionRows.size()))
            });
            return 0;
        }
        reporter.writeHeading("The largest conversations");
        for (const SessionSizeRow& sessionRow : sessionRows) {
            reporter.writeDetailLine(
                HumanReadableFormatting::formatBytes(sessionRow.payloadBytes) + "  "
                + sessionRow.sessionIdentifier + "  ("
                + HumanReadableFormatting::formatCount(sessionRow.partCount) + " parts)"
            );
        }
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Read one before deciding anything: agent-db-doctor export --session <id>"),
            string("Delete one, with a copy and a check: agent-db-doctor sessions delete --session <id>")
        });
        return 0;
    } catch (const DatabaseOpenException& openFailure) {
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        return 1;
    }
}
int SessionsCommand::deleteSession(const CommandOptions& options) {
    if (options.databaseFilePath.empty() || options.sessionIdentifier.empty()) {
        reporter.writeFailure(
            string("Deleting a session needs both a database and a session identifier:\n"
                "  agent-db-doctor sessions delete --database <path> --session <id>"),
            string()
        );
        return 2;
    }
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    if (!fileStatus.exists || fileStatus.isDirectory) {
        reporter.writeFailure(
            string("There is no database file at ") + options.databaseFilePath + ".", string()
        );
        return 1;
    }
    const SafetyCheckResult lockCheck=SafetyChecks::verifyDatabaseIsNotLocked(options.databaseFilePath);
    if (!lockCheck.passed()) {
        reporter.writeFailure(lockCheck.explanation() + "\n\nNothing was changed.", string());
        return 1;
    }
    // The copy and the result of the run outlive the connection, because putting the
    // copy back requires the file to be closed first.
    BackupRecord backup;
    bool restoreWasRequested=false;
    try {
        {
            // Everything that needs the connection open lives in this block, so the
            // handle is closed before any restore replaces the file.
            Database database(options.databaseFilePath, Database::OPEN_READ_WRITE);
            database.setBusyTimeoutMilliseconds(5000);
            if (StatisticsReader::tableExists(database, "session_message")
                || StatisticsReader::tableExists(database, "session_input")) {
                reporter.writeFailure(secondGenerationRefusal(), string());
                return 1;
            }
            if (!StatisticsReader::tableExists(database, "session")
                || !StatisticsReader::tableExists(database, "message")
                || !StatisticsReader::tableExists(database, "part")) {
                reporter.writeFailure(
                    string("That database does not have the session, message, and part tables this "
                        "command needs, so its layout is not one this tool recognises. Nothing was "
                        "changed."),
                    string()
                );
                return 1;
            }
            // What the removal would touch, counted before anything is asked. Counting
            // the payload means reading the session's own parts, which are the rows
            // about to go, and nothing else.
            long long messageCount=0;
            long long partCount=0;
            long long payloadBytes=0;
            {
                Statement countStatement(
                    database.rawHandle(),
                    "SELECT COUNT(*), COALESCE(SUM(LENGTH(p.data)), 0) FROM message m "
                    "LEFT JOIN part p ON p.message_id = m.id WHERE m.session_id=?1",
                    "a count of the session being removed"
                );
                if (countStatement.isValid()) {
                    countStatement.bindText(1, options.sessionIdentifier);
                    if (countStatement.step()) {
                        messageCount=countStatement.columnInteger(0);
                        payloadBytes=countStatement.columnInteger(1);
                    }
                }
                Statement partCountStatement(
                    database.rawHandle(),
                    "SELECT COUNT(*) FROM part WHERE message_id IN "
                    "(SELECT id FROM message WHERE session_id=?1)",
                    "a count of the session's parts"
                );
                if (partCountStatement.isValid()) {
                    partCountStatement.bindText(1, options.sessionIdentifier);
                    if (partCountStatement.step()) {
                        partCount=partCountStatement.columnInteger(0);
                    }
                }
            }
            if (messageCount==0) {
                reporter.writeFailure(
                    string("There is no session with the identifier ") + options.sessionIdentifier
                    + " in that database, so there is nothing to remove. Nothing was changed.",
                    string()
                );
                return 1;
            }
            // The sampled sessions are taken before the question, so the check
            // afterwards has something to compare against. The session being removed
            // is dropped from the sample, because it is meant to be gone.
            vector<SessionSnapshot> comparisonSnapshots;
            for (const SessionSnapshot& snapshot : SafetyChecks::captureOldestSessions(
                     database,
                     SafetyChecks::SAMPLED_SESSION_COUNT)) {
                if (snapshot.sessionIdentifier!=options.sessionIdentifier) {
                    comparisonSnapshots.push_back(snapshot);
                }
            }
            DestructiveOperationGuard guard(options, reporter);
            backup=guard.createBackup(options.databaseFilePath);
            if (!backup.wasCreated) {
                reporter.writeFailure(
                    string("A safety copy could not be made, so nothing was changed. This is "
                        "usually because there is not enough free space for a second copy of the "
                        "file."),
                    string()
                );
                return 1;
            }
            if (!guard.confirmWithUser(
                string("remove the session ") + options.sessionIdentifier + " ("
                + HumanReadableFormatting::formatCount(messageCount) + " messages, "
                + HumanReadableFormatting::formatCount(partCount) + " parts, "
                + HumanReadableFormatting::formatBytes(payloadBytes) + " of payload)",
                backup
            )) {
                reporter.writeLine("Nothing was removed. Your database is exactly as it was.");
                return 0;
            }
            // The removal is one transaction: either the whole conversation goes or
            // none of it does. Every identifier is bound rather than interpolated, so
            // an identifier with a quote in it is a strange identifier rather than a
            // strange statement.
            database.execute("BEGIN IMMEDIATE");
            const vector<string> deleteStatements={
                string("DELETE FROM part WHERE message_id IN (SELECT id FROM message WHERE session_id=?1)"),
                string("DELETE FROM message WHERE session_id=?1"),
                string("DELETE FROM session WHERE id=?1")
            };
            for (const string& deleteText : deleteStatements) {
                Statement deleteStatement(database.rawHandle(), deleteText, "the removal of a session");
                if (!deleteStatement.isValid()) {
                    database.execute("ROLLBACK");
                    restoreWasRequested=true;
                    break;
                }
                deleteStatement.bindText(1, options.sessionIdentifier);
                if (!deleteStatement.run()) {
                    database.execute("ROLLBACK");
                    restoreWasRequested=true;
                    break;
                }
            }
            if (!restoreWasRequested
                && StatisticsReader::tableExists(database, "event")
                && StatisticsReader::columnExists(database, "event", "aggregate_id")) {
                Statement eventDeleteStatement(
                    database.rawHandle(),
                    "DELETE FROM event WHERE aggregate_id=?1",
                    "the removal of a session's events"
                );
                if (eventDeleteStatement.isValid()) {
                    eventDeleteStatement.bindText(1, options.sessionIdentifier);
                    if (!eventDeleteStatement.run()) {
                        database.execute("ROLLBACK");
                        restoreWasRequested=true;
                    }
                }
            }
            if (!restoreWasRequested) {
                database.execute("COMMIT");
                // The check: the session is gone, and every sampled conversation
                // still holds its readable text.
                bool sessionIsGone=false;
                {
                    Statement goneStatement(
                        database.rawHandle(),
                        "SELECT COUNT(*) FROM session WHERE id=?1",
                        "a check that the session is gone"
                    );
                    if (goneStatement.isValid()) {
                        goneStatement.bindText(1, options.sessionIdentifier);
                        if (goneStatement.step()) {
                            sessionIsGone=goneStatement.columnInteger(0)==0;
                        }
                    }
                }
                const SafetyCheckResult survivalCheck=SafetyChecks::verifySnapshotsSurvivedUnchanged(
                    database,
                    comparisonSnapshots
                );
                if (!sessionIsGone || !survivalCheck.passed()) {
                    restoreWasRequested=true;
                } else {
                    reporter.writeHeading("Done");
                    reporter.writeLine(
                        string("Removed the session ") + options.sessionIdentifier + ": "
                        + HumanReadableFormatting::formatCount(messageCount) + " messages, "
                        + HumanReadableFormatting::formatCount(partCount) + " parts, "
                        + HumanReadableFormatting::formatBytes(payloadBytes) + " of payload."
                    );
                    reporter.writeLine(survivalCheck.explanation());
                    const bool keepTheCopy=guard.shouldKeepBackup();
                    guard.removeBackupAfterSuccess(backup);
                    reporter.writeWarning(
                        "The file is still about the same size on disk. Removing rows marks the "
                        "space as reusable but does not hand it back to the drive. Run the vacuum "
                        "command next, and it will be."
                    );
                    if (reporter.isMachineReadable()) {
                        reporter.writeMachineResult("sessions", vector<MachineField>{
                            MachineField::text("action", "delete"),
                            MachineField::text("session", options.sessionIdentifier),
                            MachineField::number("messagesRemoved", messageCount),
                            MachineField::number("partsRemoved", partCount),
                            MachineField::number("payloadBytesRemoved", payloadBytes),
                            MachineField::boolean("safetyCopyRemoved", !keepTheCopy)
                        });
                    }
                    return 0;
                }
            }
        }
    } catch (const PragmaException& pragmaFailure) {
        reporter.writeFailure(pragmaFailure.message(), string());
        return 1;
    } catch (const DatabaseOpenException& openFailure) {
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        return 1;
    } catch (const DatabaseOperationException& operationFailure) {
        reporter.writeFailure(operationFailure.message(), string());
        return 1;
    }
    // The removal stopped or its check failed, and the promise is that the database
    // goes back to the state it was found in. The connection is closed by now, which
    // is what makes the file replaceable.
    if (!restoreWasRequested || !backup.wasCreated) {
        return 1;
    }
    DestructiveOperationGuard restoringGuard(options, reporter);
    const bool restored=restoringGuard.restoreBackup(backup, options.databaseFilePath);
    bool databaseIsWhole=false;
    if (restored) {
        try {
            Database restoredDatabase(options.databaseFilePath, Database::OPEN_READ_ONLY);
            restoredDatabase.setBusyTimeoutMilliseconds(5000);
            databaseIsWhole=SafetyChecks::verifyOldestSessionStillHasReadableContent(
                restoredDatabase
            ).passed();
        } catch (const DatabaseOpenException&) {
            databaseIsWhole=false;
        }
    }
    if (restored && databaseIsWhole) {
        reporter.writeLine(
            "The safety copy was put back and the oldest session still holds its text, so your "
            "database holds exactly what it held before this run. Nothing was lost."
        );
        reporter.writeLine(
            "The copy is still at " + backup.backupFilePath + " until you delete it."
        );
    } else if (restored) {
        reporter.writeWarning(
            string("The safety copy was put back, but the check did not pass afterwards either.\n\n"
                "Compare the database with the copy at ") + backup.backupFilePath
            + " and run the recover command for the safe next step."
        );
    } else {
        reporter.writeFailure(
            string("The safety copy could not be put back automatically. It is untouched at ")
            + backup.backupFilePath
            + ".\n\nPut it back yourself with:\n  agent-db-doctor backups restore --database "
            + options.databaseFilePath,
            string()
        );
    }
    return 1;
}
int SessionsCommand::execute(const CommandOptions& options) {
    const string action=options.sessionsAction.empty() ? string("list") : options.sessionsAction;
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (action=="list") {
        return listSessions(options);
    }
    if (action=="delete") {
        return deleteSession(options);
    }
    reporter.writeFailure(
        string("There is no `sessions ") + action + "` action. The actions are: list, delete.",
        string()
    );
    return 2;
}
}
