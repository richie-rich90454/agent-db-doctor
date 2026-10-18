// The implementation of the DiagnoseCommand class.
//
// The command has one job and one rule: look, then explain. It never opens a
// database for writing, never creates a file, and never leaves anything behind.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DiagnoseCommand class declaration.
#include "AgentDbDoctor/Command/DiagnoseCommand.hpp"
// Provides the AgentRegistry, used to discover the databases.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides the IntegrityStatus enumeration.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
// Provides the SafetyCheckResult value type.
#include "AgentDbDoctor/Doctor/SafetyCheckResult.hpp"
// Provides the read counters, so `--trace-reads` can report what the run read.
#include "AgentDbDoctor/Persistence/ReadCountingVfs.hpp"
// Provides the number to text conversions used by the trace line.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::Doctor;
namespace AgentDbDoctor::Command {
DiagnoseCommand::DiagnoseCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string DiagnoseCommand::describe() const {
    return string(
        "Look at every supported coding agent database and report its size, what is inside it, "
        "and whether anything is wrong. Nothing is changed."
    );
}
string DiagnoseCommand::commandName() const {
    return string("diagnose");
}
void DiagnoseCommand::writePurpose(const CommandOptions& options) const {
    if (options.databaseFilePath.empty()) {
        reporter.writeLine("Looking for the databases of every supported coding agent.");
    } else {
        reporter.writeLine("Looking at " + options.databaseFilePath + ".");
    }
}
int DiagnoseCommand::healthyDatabaseCount() const {
    return healthyCount;
}
int DiagnoseCommand::problemDatabaseCount() const {
    return problemCount;
}
void DiagnoseCommand::resetCounts() {
    healthyCount=0;
    problemCount=0;
}
vector<string> DiagnoseCommand::resolveDatabasePaths(
    const CommandOptions& options,
    vector<string>& agentNamesOut
) const {
    vector<string> databasePaths;
    agentNamesOut.clear();
    if (!options.databaseFilePath.empty()) {
        // The user named a file, so exactly that file is looked at, whatever agent
        // it belongs to. This is how someone investigates a database the tool does
        // not know how to find.
        databasePaths.push_back(options.databaseFilePath);
        agentNamesOut.push_back(options.agentName);
        return databasePaths;
    }
    // The user named no file, so every supported agent is asked where its data is.
    // The discovery runs once and the results are then grouped by agent name, in
    // the fixed agent order. Asking the registry once per agent re-ran the whole
    // discovery sixty two times, which on a machine with many databases meant
    // thousands of unnecessary directory listings.
    const AgentRegistry registry;
    const vector<DiscoveredDatabase> discoveredDatabases=registry.discoverAllDatabases();
    for (const AgentDescriptor& descriptor : registry.allAgents()) {
        if (!options.agentName.empty() && descriptor.displayName!=options.agentName) {
            continue;
        }
        for (const DiscoveredDatabase& discovered : discoveredDatabases) {
            if (discovered.agentName!=descriptor.displayName) {
                continue;
            }
            databasePaths.push_back(discovered.filePath);
            agentNamesOut.push_back(discovered.agentName);
        }
    }
    return databasePaths;
}
int DiagnoseCommand::execute(const CommandOptions& options) {
    resetCounts();
    if (options.traceReadCounts) {
        // The counters are reset here so the trace reports this run and not the
        // process's whole life.
        ReadCountingVfs::resetCounters();
    }
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
        writePurpose(options);
    }
    vector<string> agentNames;
    const vector<string> databasePaths=resolveDatabasePaths(options, agentNames);
    if (options.verbose) {
        // The verbose flag exists so a user can see the discovery itself, not just
        // its results: how many paths were found, and which one is being examined
        // at each step.
        reporter.writeDetailLine(
            string("Discovery finished with ") + to_string(databasePaths.size())
            + " path(s) to look at."
        );
    }
    if (databasePaths.empty()) {
        reporter.writeHeading("Nothing was found");
        if (options.agentName.empty()) {
            reporter.writeLine(
                "No supported coding agent has a database on this machine. That is not a problem: "
                "it means the tool has nothing to do until one is used."
            );
        } else {
            reporter.writeLine(
                string("No database was found for ") + options.agentName
                + ". The tool looked in the places that version of the agent uses on this system."
            );
        }
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Install and use one of the supported agents, then run this command again."),
            string("If you know where the database is, pass it directly: agent-db-doctor diagnose --database /path/to/file")
        });
        if (options.traceReadCounts) {
            reporter.writeDetailLine(
                string("Reads: ") + HumanReadableFormatting::formatCount(ReadCountingVfs::countedBytes())
                + " bytes in "
                + HumanReadableFormatting::formatCount(ReadCountingVfs::countedReadCalls())
                + " read calls, counted through SQLite's own file layer."
            );
        }
        return 0;
    }
    // Every report is printed in turn, with a blank line between them so they read
    // as separate sections rather than as one run on paragraph.
    // A folder is counted apart from the two verdicts, because it is neither a
    // database that passed nor a database that failed.
    int folderCount=0;
    for (size_t reportIndex=0; reportIndex<databasePaths.size(); ++reportIndex) {
        if (options.verbose) {
            const string agentLabel=reportIndex<agentNames.size() && !agentNames[reportIndex].empty()
                ? agentNames[reportIndex]
                : string("an unknown agent");
            reporter.writeDetailLine(
                string("Looking at the ") + agentLabel + " data at " + databasePaths[reportIndex]
            );
        }
        const DiagnosisReport report=doctor.diagnoseDatabase(
            databasePaths[reportIndex],
            reportIndex<agentNames.size() ? agentNames[reportIndex] : string(),
            options.runIntegrityCheck
        );
        reporter.writeDiagnosisReport(report);
        reporter.writeLine("");
        // A folder of plain text files is not a database and has no integrity to
        // check, so it is never counted as a problem. Counting it as one would
        // make every install of a text based agent look like a fault.
        const bool isFolderOfTextFiles=report.getIsDirectory();
        if (isFolderOfTextFiles) {
            // A folder is counted separately from both healthy and needing attention.
            // Calling it healthy would claim an inspection that never happened, and
            // calling it a problem would make every text based agent look broken.
            folderCount++;
        } else if (report.isHealthy()) {
            healthyCount++;
        } else {
            problemCount++;
        }
        // The next steps come from the report itself, so they are specific to what
        // was found rather than a fixed list.
        reporter.writeSuggestedNextSteps(report.getSuggestedNextSteps());
        reporter.writeLine("");
    }
    // The closing line is a count rather than a judgement, because a user who ran
    // the command on six databases needs to know how many were fine and how many
    // need attention without scrolling back.
    string summary=string("Looked at ") + to_string(databasePaths.size())
        + (databasePaths.size()==1 ? " path. " : " paths. ")
        + to_string(healthyCount) + " healthy, " + to_string(problemCount) + " need"
        + (problemCount==1 ? "s" : "") + " attention";
    if (folderCount>0) {
        summary += ", " + to_string(folderCount) + (folderCount==1
            ? " was a folder of text files rather than a database"
            : " were folders of text files rather than databases");
    }
    summary += ".";
    reporter.writeHeading("Summary");
    reporter.writeLine(summary);
    if (options.traceReadCounts) {
        reporter.writeDetailLine(
            string("Reads: ") + HumanReadableFormatting::formatCount(ReadCountingVfs::countedBytes())
            + " bytes in "
            + HumanReadableFormatting::formatCount(ReadCountingVfs::countedReadCalls())
            + " read calls, counted through SQLite's own file layer."
        );
    }
    // The exit code tells a script whether to stop. Anything that needs a human is a
    // one, which is what makes the command usable from a monitoring script.
    return problemCount>0 ? 1 : 0;
}
}
