// The implementation of the WhyCommand class.
//
// Every number this command prints is a number another command already computed,
// which is why it is cheap: the diagnostic report gives the size, the free pages,
// and the redundant snapshot count; the page accounting gives the largest tables;
// the growth record gives the rate. The causes are then ranked by the bytes they
// would give back, and the answer ends with the one command that would take the
// largest of them.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the WhyCommand class declaration.
#include "AgentDbDoctor/Command/WhyCommand.hpp"
// Provides the doctor, whose cheap pass supplies most of the numbers.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
// Provides the space accounting, for the largest tables.
#include "AgentDbDoctor/Doctor/SpaceAttribution.hpp"
// Provides the growth record.
#include "AgentDbDoctor/Service/HistoryStore.hpp"
// Provides the Database wrapper, for the page accounting.
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
using namespace AgentDbDoctor::Service;
namespace AgentDbDoctor::Command {
namespace {
// One cause: a sentence, the bytes it would give back when that is known, and the
// command that would take it.
struct SizedCause final {
    string explanation;
    long long bytesThatWouldComeBack=0;
    string command;
};
}
WhyCommand::WhyCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string WhyCommand::describe() const {
    return string(
        "Explain why one database is as large as it is: the ranked causes, the bytes each would "
        "give back, and the one command worth running next. Nothing is changed."
    );
}
string WhyCommand::commandName() const {
    return string("why");
}
int WhyCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (options.databaseFilePath.empty()) {
        reporter.writeFailure(
            string("This command needs to know which database to explain. Tell it with --database."),
            string()
        );
        return 1;
    }
    try {
        AgentDatabaseDoctor doctor;
        const DiagnosisReport report=doctor.diagnoseDatabase(
            options.databaseFilePath,
            options.agentName,
            false
        );
        if (!report.getFileExists()) {
            reporter.writeFailure(
                string("There is no database file at ") + options.databaseFilePath + ".", string()
            );
            return 1;
        }
        const long long fileSizeBytes=report.getLogicalSizeBytes();
        vector<SizedCause> causes;
        // The free pages are the first cause and the one that is always exact: they
        // are space inside the file that nothing is using, and a vacuum hands all of
        // it back.
        const long long freeBytes=report.getReclaimableBytes();
        if (freeBytes>0) {
            causes.push_back(SizedCause{
                string("Unused space inside the file: ") + HumanReadableFormatting::formatBytes(freeBytes)
                + " of the " + HumanReadableFormatting::formatBytes(fileSizeBytes)
                + " is pages nothing is using any more.",
                freeBytes,
                string("agent-db-doctor vacuum --database \"") + options.databaseFilePath + "\""
            });
        }
        // The redundant snapshots come next when there are any. The exact number of
        // bytes they hold is not known without compacting, so the cause is ranked by
        // the row count and the estimate is left to the preview, which counts
        // exactly.
        if (report.getCompactionSupported() && report.getRedundantEventRowCount()>0) {
            causes.push_back(SizedCause{
                string("Redundant snapshots in the event log: ")
                + HumanReadableFormatting::formatCount(report.getRedundantEventRowCount())
                + " rows are older copies of messages that still exist in their newest form.",
                0,
                string("agent-db-doctor compact --database \"") + options.databaseFilePath
                + "\" --dry-run"
            });
        }
        // The schema generation is worth saying when it is the reason compaction has
        // nothing to do.
        if (report.getCompactionSupported() && report.getRedundantEventRowCount()==0) {
            causes.push_back(SizedCause{
                string("Nothing to compact: ") + report.getSchemaGenerationDescription()
                + ". The newest snapshot of every message is already the only copy of it.",
                0,
                string()
            });
        }
        // The large tables are the third cause, and they are information rather than
        // something a command would fix: they are the conversation itself.
        Database database(options.databaseFilePath, Database::OPEN_READ_ONLY);
        database.setBusyTimeoutMilliseconds(5000);
        const vector<SpaceUsageRow> usageRows=SpaceAttribution::tableAndIndexSizes(database);
        for (size_t rowIndex=0; rowIndex<usageRows.size() && rowIndex<3; ++rowIndex) {
            if (usageRows[rowIndex].isIndex) {
                continue;
            }
            causes.push_back(SizedCause{
                string("Stored content: the ") + usageRows[rowIndex].name + " table holds "
                + HumanReadableFormatting::formatBytes(usageRows[rowIndex].bytes)
                + ", which is the conversation itself.",
                0,
                string()
            });
        }
        // The growth rate is the last cause, and the only one that predicts.
        const vector<HistoryPoint> recordedPoints=HistoryStore::pointsFor(options.databaseFilePath, 0);
        string growthSentence;
        if (recordedPoints.size()>=2) {
            const HistoryPoint firstPoint=recordedPoints.front();
            const HistoryPoint lastPoint=recordedPoints.back();
            const long long elapsedSeconds=lastPoint.epochSeconds-firstPoint.epochSeconds;
            if (elapsedSeconds>0 && lastPoint.sizeBytes>firstPoint.sizeBytes) {
                const double bytesPerDay=static_cast<double>(lastPoint.sizeBytes-firstPoint.sizeBytes)
                    / (static_cast<double>(elapsedSeconds) / 86400.0);
                growthSentence=string("It has been growing by about ")
                    + HumanReadableFormatting::formatBytes(static_cast<long long>(bytesPerDay))
                    + " per day over the recorded window.";
            }
        }
        // The best cause is the one that would give back the most, and the free
        // pages are the only one whose size is exact, so they come first when they
        // are there.
        string nextCommand;
        for (const SizedCause& cause : causes) {
            if (!cause.command.empty()) {
                nextCommand=cause.command;
                break;
            }
        }
        if (reporter.isMachineReadable()) {
            string jsonText="{\"event\":\"why\"";
            jsonText+=",\"path\":\"" + ConsoleReport::escapeForJson(options.databaseFilePath) + "\"";
            jsonText+=",\"sizeBytes\":" + to_string(fileSizeBytes);
            jsonText+=",\"freeBytes\":" + to_string(freeBytes);
            jsonText+=",\"redundantEventRows\":" + to_string(report.getRedundantEventRowCount());
            jsonText+=",\"growthSentence\":\"" + ConsoleReport::escapeForJson(growthSentence) + "\"";
            jsonText+=",\"causes\":[";
            for (size_t causeIndex=0; causeIndex<causes.size(); ++causeIndex) {
                if (causeIndex>0) {
                    jsonText+=",";
                }
                jsonText+="{\"explanation\":\"" + ConsoleReport::escapeForJson(causes[causeIndex].explanation)
                    + "\",\"bytesThatWouldComeBack\":" + to_string(causes[causeIndex].bytesThatWouldComeBack)
                    + ",\"command\":\"" + ConsoleReport::escapeForJson(causes[causeIndex].command) + "\"}";
            }
            jsonText+="],\"nextCommand\":\"" + ConsoleReport::escapeForJson(nextCommand) + "\"}";
            reporter.writeMachineObject(jsonText);
            reporter.writeMachineResult("why", vector<MachineField>{
                MachineField::number("sizeBytes", fileSizeBytes),
                MachineField::number("freeBytes", freeBytes),
                MachineField::number("redundantEventRows", report.getRedundantEventRowCount())
            });
            return 0;
        }
        reporter.writeLine(options.databaseFilePath);
        reporter.writeLine("Size: " + HumanReadableFormatting::formatBytes(fileSizeBytes) + ".");
        if (!growthSentence.empty()) {
            reporter.writeLine(growthSentence);
        }
        reporter.writeHeading("Why");
        for (const SizedCause& cause : causes) {
            reporter.writeLine("* " + cause.explanation);
        }
        if (nextCommand.empty()) {
            reporter.writeLine(
                "Nothing here can be reclaimed safely: every page is in use and every message "
                "exists once. That is the good answer."
            );
            return 0;
        }
        reporter.writeHeading("What would help most");
        reporter.writeLine("Run this:");
        reporter.writeLine("    " + nextCommand);
        vector<string> suggestedSteps;
        if (freeBytes>0) {
            suggestedSteps.push_back(
                string("The vacuum hands the unused pages back to the drive.")
            );
        }
        if (report.getRedundantEventRowCount()>0) {
            suggestedSteps.push_back(
                string("The compact preview counts exactly what compaction would remove, and changes nothing.")
            );
        }
        suggestedSteps.push_back(
            string("explain-space --sessions 10 ranks the largest conversations if you want to look further.")
        );
        reporter.writeSuggestedNextSteps(suggestedSteps);
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
