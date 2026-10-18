// The implementation of the ExplainSpaceCommand class.
//
// The measurement is deliberately separate from diagnose. A diagnostic pass is a
// few metadata calls and a header read; this command walks the database's page
// accounting, which is fast but proportional to the file, and with --sessions it
// reads the stored payload. Both are things a user should ask for on purpose rather
// than pay for on every look.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides `sort`, which orders the session ranking.
#include <algorithm>
// Provides the ExplainSpaceCommand class declaration.
#include "AgentDbDoctor/Command/ExplainSpaceCommand.hpp"
// Provides PlatformMetadata, for the file size.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the Database wrapper.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides StatisticsReader, for the header fields.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the space accounting.
#include "AgentDbDoctor/Doctor/SpaceAttribution.hpp"
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
ExplainSpaceCommand::ExplainSpaceCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string ExplainSpaceCommand::describe() const {
    return string(
        "Report where a database's bytes actually are: the bytes each table and index occupies, "
        "read from SQLite's own page accounting, and with --sessions the largest conversations. "
        "Nothing is changed."
    );
}
string ExplainSpaceCommand::commandName() const {
    return string("explain-space");
}
int ExplainSpaceCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (options.databaseFilePath.empty()) {
        reporter.writeFailure(
            string("This command needs to know which database to measure. Tell it with "
                "--database; `agents` prints where each agent keeps its data."),
            string()
        );
        return 1;
    }
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(options.databaseFilePath);
    if (!fileStatus.exists || fileStatus.isDirectory) {
        reporter.writeFailure(
            string("There is no database file at ") + options.databaseFilePath + ".", string()
        );
        return 1;
    }
    try {
        Database database(options.databaseFilePath, Database::OPEN_READ_ONLY);
        database.setBusyTimeoutMilliseconds(5000);
        const SqliteHeaderFields headerFields=StatisticsReader::readHeaderFields(options.databaseFilePath);
        if (!headerFields.isValidSqliteFile) {
            reporter.writeFailure(
                string("The file at ") + options.databaseFilePath + " is not a SQLite database.",
                string()
            );
            return 1;
        }
        const long long fileSizeBytes=fileStatus.logicalSizeBytes;
        const long long freeBytes=StatisticsReader::reclaimableBytesFromFreeList(headerFields);
        const vector<SpaceUsageRow> usageRows=SpaceAttribution::tableAndIndexSizes(database);
        if (usageRows.empty()) {
            reporter.writeFailure(
                string("This build of SQLite cannot report the page accounting this command needs, "
                    "so the division of space cannot be shown. The file's size and free pages are "
                    "still reported by diagnose."),
                string()
            );
            return 1;
        }
        long long accountedBytes=0;
        for (const SpaceUsageRow& usageRow : usageRows) {
            accountedBytes+=usageRow.bytes;
        }
        if (reporter.isMachineReadable()) {
            string jsonText="{\"event\":\"space\"";
            jsonText+=",\"path\":\"" + ConsoleReport::escapeForJson(options.databaseFilePath) + "\"";
            jsonText+=",\"sizeBytes\":" + to_string(fileSizeBytes);
            jsonText+=",\"pageSizeBytes\":" + to_string(headerFields.pageSizeBytes);
            jsonText+=",\"pageCount\":" + to_string(headerFields.pageCount);
            jsonText+=",\"freeBytes\":" + to_string(freeBytes);
            jsonText+=",\"entries\":[";
            for (size_t entryIndex=0; entryIndex<usageRows.size(); ++entryIndex) {
                if (entryIndex>0) {
                    jsonText+=",";
                }
                jsonText+="{\"name\":\"" + ConsoleReport::escapeForJson(usageRows[entryIndex].name)
                    + "\",\"bytes\":" + to_string(usageRows[entryIndex].bytes)
                    + ",\"isIndex\":" + (usageRows[entryIndex].isIndex ? "true" : "false") + "}";
            }
            jsonText+="]}";
            reporter.writeMachineObject(jsonText);
        } else {
            reporter.writeLine(options.databaseFilePath);
            reporter.writeLine("Size:        " + HumanReadableFormatting::formatBytes(fileSizeBytes));
            reporter.writeLine("Page size:   " + HumanReadableFormatting::formatCount(headerFields.pageSizeBytes) + " bytes");
            reporter.writeLine("Pages:       " + HumanReadableFormatting::formatCount(headerFields.pageCount));
            reporter.writeLine("Free pages:  " + HumanReadableFormatting::formatBytes(freeBytes)
                + " (a vacuum hands this back)");
            reporter.writeLine(ConsoleReport::separatorLine());
            // The rows are printed largest first, which is the order that answers the
            // question. The share is of the file's size, because that is the number
            // the reader has in their head.
            for (const SpaceUsageRow& usageRow : usageRows) {
                const string shareText=fileSizeBytes>0
                    ? "  " + HumanReadableFormatting::formatPercentage(
                        static_cast<double>(usageRow.bytes) / static_cast<double>(fileSizeBytes),
                        1
                    )
                    : string();
                reporter.writeDetailLine(
                    HumanReadableFormatting::formatBytes(usageRow.bytes) + shareText + "  "
                    + usageRow.name + (usageRow.isIndex ? "  [index]" : "")
                );
            }
            reporter.writeLine(ConsoleReport::separatorLine());
            reporter.writeLine(
                string("Accounted for: ") + HumanReadableFormatting::formatBytes(accountedBytes)
                + " across " + to_string(usageRows.size()) + " tables and indexes."
            );
        }
        long long sessionPayloadBytes=0;
        if (options.sessionsToRank>0) {
            const vector<SessionSizeRow> sessionRows=SpaceAttribution::largestSessions(
                database,
                options.sessionsToRank
            );
            for (const SessionSizeRow& sessionRow : sessionRows) {
                sessionPayloadBytes+=sessionRow.payloadBytes;
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
            } else if (!sessionRows.empty()) {
                reporter.writeHeading("The largest conversations");
                reporter.writeDetailLine(
                    "Measured by the payload they store, which is what a session deletion gives back."
                );
                for (const SessionSizeRow& sessionRow : sessionRows) {
                    reporter.writeDetailLine(
                        HumanReadableFormatting::formatBytes(sessionRow.payloadBytes) + "  "
                        + sessionRow.sessionIdentifier + "  ("
                        + HumanReadableFormatting::formatCount(sessionRow.partCount) + " parts)"
                    );
                }
                reporter.writeLine("");
                reporter.writeLine(
                    string("These ") + to_string(sessionRows.size()) + " sessions hold "
                    + HumanReadableFormatting::formatBytes(sessionPayloadBytes)
                    + " of stored payload between them."
                );
                reporter.writeSuggestedNextSteps(vector<string>{
                    string("Read one before deciding anything: agent-db-doctor export --session <id>"),
                    string("Delete one with a copy and a check: agent-db-doctor sessions delete --session <id>")
                });
            }
        } else if (!reporter.isMachineReadable()) {
            reporter.writeSuggestedNextSteps(vector<string>{
                string("Run this command with --sessions to rank the conversations by the payload they store."),
                string("Run the vacuum command to hand the free pages back to the drive.")
            });
        }
        if (reporter.isMachineReadable()) {
            reporter.writeMachineResult("explain-space", vector<MachineField>{
                MachineField::number("sizeBytes", fileSizeBytes),
                MachineField::number("freeBytes", freeBytes),
                MachineField::number("accountedBytes", accountedBytes),
                MachineField::number("sessionPayloadBytes", sessionPayloadBytes)
            });
        }
        return 0;
    } catch (const DatabaseOpenException& openFailure) {
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        return 1;
    } catch (const DatabaseOperationException& operationFailure) {
        reporter.writeFailure(operationFailure.message(), string());
        return 1;
    }
}
}
