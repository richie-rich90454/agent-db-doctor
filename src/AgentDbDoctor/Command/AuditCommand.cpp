// The implementation of the AuditCommand class.
//
// The two actions differ in one way only: `tail`, the default, shows the newest
// entries and nothing else, while `list` also says how many entries the whole log
// holds, so a user can tell twenty recent lines from a log that holds twenty
// lines in total. Both show their selection oldest first, because that is the way
// a sequence of events reads, and both stop at the limit so that a long serving
// machine cannot scroll the newest entry, the one that matters, off the screen.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AuditCommand class declaration.
#include "AgentDbDoctor/Command/AuditCommand.hpp"
// Provides PlatformMetadata, used to tell a missing log from an unreadable one.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Service;
namespace AgentDbDoctor::Command {
// The number of entries shown when the user does not name one. It is the same
// number the option carries by default, repeated here so the command does not
// depend on that default meaning anything.
static constexpr int DEFAULT_ENTRY_LIMIT=20;
AuditCommand::AuditCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string AuditCommand::describe() const {
    return string(
        "Show what the background service changed by itself: one line per compacting or vacuum step "
        "it performed, with the size before and after. Reads only."
    );
}
string AuditCommand::commandName() const {
    return string("audit");
}
void AuditCommand::writeEntryLine(const AuditEntry& entry) const {
    const string exitCodeText=entry.exitCode==0
        ? string()
        : string(" [exit code ") + to_string(entry.exitCode) + "]";
    reporter.writeLine(
        HumanReadableFormatting::formatTimestamp(entry.epochSeconds * 1000)
        + "  " + entry.action
        + "  " + entry.databaseFilePath
        + "  " + HumanReadableFormatting::formatBytes(entry.sizeBeforeBytes)
        + " to " + HumanReadableFormatting::formatBytes(entry.sizeAfterBytes)
        + "  " + entry.outcome
        + exitCodeText
    );
    // The safety copy is not part of the one line, because it is not part of
    // every entry, but a user checking what a step did needs to know where the
    // copy was, so it is shown under the line that mentions it.
    if (!entry.backupFilePath.empty()) {
        reporter.writeDetailLine("Safety copy: " + entry.backupFilePath);
    }
}
int AuditCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    const string action=options.auditAction.empty() ? string("tail") : options.auditAction;
    if (action!="list" && action!="tail") {
        reporter.writeLine(
            string("The action ") + action + " is not one this command knows. The choices are:"
        );
        reporter.writeDetailLine("list  - the newest entries, and how many the whole log holds");
        reporter.writeDetailLine("tail  - the newest entries only, which is the default");
        reporter.writeSuggestedNextSteps(vector<string>{
            string("agent-db-doctor audit tail"),
            string("agent-db-doctor audit list")
        });
        return 1;
    }
    const int entryLimit=options.auditLimit>0 ? options.auditLimit : DEFAULT_ENTRY_LIMIT;
    const vector<AuditEntry> allEntries=AuditLog::readAll();
    if (allEntries.empty()) {
        // An empty log has two very different reasons, and the user deserves to
        // know which one applies: nothing has happened yet, or the file is there
        // and cannot be read as entries.
        const bool storeExists=PlatformMetadata::readFileMetadata(AuditLog::storeFilePath()).exists;
        if (reporter.isMachineReadable()) {
            reporter.writeMachineObject("{\"event\":\"audit\",\"entries\":[]}");
            reporter.writeMachineResult("audit", vector<MachineField>{
                MachineField::number("entryCount", 0)
            });
        }
        if (storeExists) {
            reporter.writeLine(
                string("The audit log at ") + AuditLog::storeFilePath()
                + " holds no entries that can be read. The file is there, so either every entry in it "
                  "is damaged or it was written by something else. Nothing is changed by this command; "
                  "look at the file yourself if you did not expect it to be empty."
            );
        } else {
            reporter.writeLine(
                "Nothing has been recorded yet. The background service writes one entry here for every "
                "step of automatic maintenance it performs, and it only performs one when a database is "
                "above its urgent threshold, allowed to be changed, idle, and not in use."
            );
            reporter.writeSuggestedNextSteps(vector<string>{
                string("agent-db-doctor service status, to see whether the service is running"),
                string("agent-db-doctor service install, to start watching the databases")
            });
        }
        return 0;
    }
    // The newest `entryLimit` entries, kept in their original order so the
    // newest one is printed last, where a reader's eye ends up.
    const size_t selectedEntryCount=allEntries.size()<static_cast<size_t>(entryLimit)
        ? allEntries.size()
        : static_cast<size_t>(entryLimit);
    const size_t firstSelectedIndex=allEntries.size()-selectedEntryCount;
    const vector<AuditEntry> selectedEntries(
        allEntries.begin() + static_cast<ptrdiff_t>(firstSelectedIndex),
        allEntries.end()
    );
    if (reporter.isMachineReadable()) {
        string jsonText="{\"event\":\"audit\",\"entries\":[";
        for (size_t entryIndex=0; entryIndex<selectedEntries.size(); ++entryIndex) {
            if (entryIndex>0) {
                jsonText+=",";
            }
            const AuditEntry& entry=selectedEntries[entryIndex];
            jsonText+="{\"epochSeconds\":" + to_string(entry.epochSeconds);
            jsonText+=",\"action\":\"" + ConsoleReport::escapeForJson(entry.action) + "\"";
            jsonText+=",\"path\":\"" + ConsoleReport::escapeForJson(entry.databaseFilePath) + "\"";
            jsonText+=",\"sizeBeforeBytes\":" + to_string(entry.sizeBeforeBytes);
            jsonText+=",\"sizeAfterBytes\":" + to_string(entry.sizeAfterBytes);
            jsonText+=",\"backupPath\":\"" + ConsoleReport::escapeForJson(entry.backupFilePath) + "\"";
            jsonText+=",\"outcome\":\"" + ConsoleReport::escapeForJson(entry.outcome) + "\"";
            jsonText+=",\"exitCode\":" + to_string(entry.exitCode) + "}";
        }
        jsonText+="]}";
        reporter.writeMachineObject(jsonText);
        reporter.writeMachineResult("audit", vector<MachineField>{
            MachineField::number("entryCount", static_cast<long long>(selectedEntries.size()))
        });
        return 0;
    }
    for (const AuditEntry& entry : selectedEntries) {
        writeEntryLine(entry);
    }
    if (action=="list") {
        reporter.writeLine(
            string("Showing the newest ") + to_string(selectedEntries.size()) + " of "
            + to_string(allEntries.size()) + (allEntries.size()==1
                ? " entry in the log."
                : " entries in the log.")
        );
    }
    return 0;
}
}
