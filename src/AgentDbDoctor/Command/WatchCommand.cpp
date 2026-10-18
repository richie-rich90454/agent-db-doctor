// The implementation of the WatchCommand class.
//
// One snapshot is discovery plus one metadata call per database, which is the same
// budget the watchdog tick lives on. The growth beside each row is the difference
// from the previous snapshot in this process, so the first render shows no growth
// and the second one shows what happened in the interval. The screen is cleared only
// when the output is a terminal, so a redirected run produces clean lines.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `fputs` and `fflush`, used to clear a terminal screen.
#include <cstdio>
// Provides `vector`, a growable array of values and the sort that orders the table.
#include <vector>
#include <algorithm>
// Provides the WatchCommand class declaration.
#include "AgentDbDoctor/Command/WatchCommand.hpp"
// Provides the registry, which finds every database.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides PlatformMetadata, for the sizes and the sleep.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
namespace {
// One row of the table.
struct WatchRow final {
    string filePath;
    string agentName;
    long long sizeBytes=0;
    long long growthBytes=0;
    bool isSqliteDatabase=false;
};
}
WatchCommand::WatchCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string WatchCommand::describe() const {
    return string(
        "Watch every database from the terminal: a live table sorted by size, with the growth "
        "since the last refresh. Sizes come from file metadata, so no database is opened."
    );
}
string WatchCommand::commandName() const {
    return string("watch");
}
long long WatchCommand::renderOneSnapshot() {
    const AgentRegistry registry;
    const vector<DiscoveredDatabase> discoveredDatabases=registry.discoverAllDatabases();
    // The previous sizes are a function local static so the loop below sees them
    // between refreshes without the command keeping state on the heap.
    static vector<pair<string, long long>> previousSizes;
    vector<WatchRow> rows;
    for (const DiscoveredDatabase& discovered : discoveredDatabases) {
        WatchRow row;
        row.filePath=discovered.filePath;
        row.agentName=discovered.agentName;
        row.isSqliteDatabase=discovered.isSqliteDatabase;
        row.sizeBytes=PlatformMetadata::readFileMetadata(discovered.filePath).logicalSizeBytes;
        for (const pair<string, long long>& previous : previousSizes) {
            if (previous.first==row.filePath) {
                row.growthBytes=row.sizeBytes-previous.second;
                break;
            }
        }
        rows.push_back(std::move(row));
    }
    // The previous table is replaced by this one every refresh, so the growth is
    // always measured against the last render.
    previousSizes.clear();
    long long totalBytes=0;
    for (const WatchRow& row : rows) {
        previousSizes.push_back(pair<string, long long>(row.filePath, row.sizeBytes));
        totalBytes+=row.sizeBytes;
    }
    sort(
        rows.begin(),
        rows.end(),
        [](const WatchRow& left, const WatchRow& right) {
            return left.sizeBytes>right.sizeBytes;
        }
    );
    if (reporter.isMachineReadable()) {
        string jsonText="{\"event\":\"watch\",\"databases\":[";
        for (size_t rowIndex=0; rowIndex<rows.size(); ++rowIndex) {
            const WatchRow& row=rows[rowIndex];
            if (rowIndex>0) {
                jsonText+=",";
            }
            jsonText+="{\"path\":\"" + ConsoleReport::escapeForJson(row.filePath)
                + "\",\"agent\":\"" + ConsoleReport::escapeForJson(row.agentName)
                + "\",\"sqlite\":" + (row.isSqliteDatabase ? "true" : "false")
                + ",\"sizeBytes\":" + to_string(row.sizeBytes)
                + ",\"growthBytes\":" + to_string(row.growthBytes) + "}";
        }
        jsonText+="],\"totalBytes\":" + to_string(totalBytes) + "}";
        reporter.writeMachineObject(jsonText);
        return totalBytes;
    }
    if (reporter.isTerminalAttached()) {
        // Clearing the screen is only for a terminal. A redirected run gets clean
        // lines, which is what a script reading this wants.
        fputs("\x1B[2J\x1B[H", stdout);
        fflush(stdout);
    }
    reporter.writeLine(
        string("Watching ") + to_string(rows.size()) + " databases, "
        + HumanReadableFormatting::formatBytes(totalBytes) + " in total."
    );
    reporter.writeLine("Sizes come from file metadata; no database is opened. Press Ctrl+C to stop.");
    reporter.writeLine(ConsoleReport::separatorLine());
    for (const WatchRow& row : rows) {
        string growthText;
        if (row.growthBytes>0) {
            growthText="  +" + HumanReadableFormatting::formatBytes(row.growthBytes);
        } else if (row.growthBytes<0) {
            growthText="  -" + HumanReadableFormatting::formatBytes(-row.growthBytes);
        }
        reporter.writeLine(
            HumanReadableFormatting::formatBytes(row.sizeBytes) + "  " + row.agentName
            + growthText
        );
        reporter.writeDetailLine(row.filePath);
    }
    return totalBytes;
}
int WatchCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (options.machineReadableOutput || options.watchOnce) {
        // A single snapshot, for a script or a test.
        renderOneSnapshot();
        if (reporter.isMachineReadable()) {
            reporter.writeMachineResult("watch", vector<MachineField>{
                MachineField::number("snapshots", 1)
            });
        }
        return 0;
    }
    const int intervalSeconds=options.watchIntervalSeconds>0 ? options.watchIntervalSeconds : 5;
    while (true) {
        renderOneSnapshot();
        PlatformMetadata::sleepMilliseconds(intervalSeconds * 1000);
    }
}
}
