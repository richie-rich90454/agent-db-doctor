// The background scan: discovery, diagnosis, and session listing off thread.
//
// The worker owns its registry and doctor locally, so the interface thread
// never touches a database. Reports are moved into the snapshot under the
// lock when each database finishes, which is what lets the dashboard fill in
// progressively instead of appearing all at once.
// Provides `string`, used for the status and log lines.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the shared state this worker publishes to.
#include "GuiState.hpp"
// Provides the registry that finds every agent's data.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides the doctor that diagnoses one database without writing to it.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
// Provides the Database wrapper for the session browser query.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the Statement class for the session browser query.
#include "AgentDbDoctor/Persistence/Statement.hpp"
// Provides PlatformMetadata, for the monotonic clock around the scan.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Gui {
namespace {
// How many sessions the browser keeps per database. Two hundred rows render
// instantly and cover every realistic "find that conversation" scroll.
constexpr int MAXIMUM_BROWSER_SESSIONS=200;
// Reads the newest sessions of a prunable database. The column names are
// discovered from the table itself rather than assumed: the bundled test
// schema has no title column, and a future schema may rename one. Anything
// unexpected yields an empty list rather than an error, because the browser
// is a convenience and the report is the record.
vector<SessionRow> readNewestSessions(const string& databaseFilePath) {
    vector<SessionRow> sessionRows;
    try {
        Database database(databaseFilePath, Database::OPEN_READ_ONLY);
        database.setBusyTimeoutMilliseconds(2000);
        bool hasTitleColumn=false;
        bool hasTimeColumn=false;
        string timeColumnName;
        {
            Statement columns(
                database.rawHandle(),
                "SELECT name FROM pragma_table_info('session')",
                "the columns of the session table");
            if (!columns.isValid()) {
                return sessionRows;
            }
            while (columns.step()) {
                // `pragma_table_info` names the column in its second field;
                // the first field is only the position number.
                const string columnName=columns.columnText(1);
                if (columnName=="title") {
                    hasTitleColumn=true;
                }
                if (timeColumnName.empty()
                    && (columnName=="time_created" || columnName=="time_updated"
                        || columnName=="created_at" || columnName=="updated_at")) {
                    timeColumnName=columnName;
                    hasTimeColumn=true;
                }
            }
        }
        string sessionQuery="SELECT id";
        if (hasTitleColumn) {
            sessionQuery+=", title";
        }
        if (hasTimeColumn) {
            sessionQuery+=", " + timeColumnName;
        }
        sessionQuery+=" FROM session ORDER BY rowid DESC LIMIT "
            + to_string(MAXIMUM_BROWSER_SESSIONS);
        Statement sessions(database.rawHandle(), sessionQuery, "the newest sessions");
        if (!sessions.isValid()) {
            return sessionRows;
        }
        while (sessions.step()) {
            SessionRow row;
            row.sessionIdentifier=sessions.columnText(0);
            if (row.sessionIdentifier.empty()) {
                continue;
            }
            if (hasTitleColumn) {
                row.sessionTitle=sessions.columnText(1);
                if (row.sessionTitle.empty()) {
                    row.sessionTitle=row.sessionIdentifier;
                }
            } else {
                row.sessionTitle=row.sessionIdentifier;
            }
            if (hasTimeColumn) {
                row.epochSeconds=sessions.columnInteger(hasTitleColumn ? 2 : 1);
            }
            sessionRows.push_back(std::move(row));
            if (static_cast<int>(sessionRows.size())>=MAXIMUM_BROWSER_SESSIONS) {
                break;
            }
        }
    } catch (...) {
        // The browser must never break the scan: an unreadable table means an
        // empty browser, and the diagnosis report already says what is wrong.
        sessionRows.clear();
    }
    return sessionRows;
}
// Appends one console line under the lock, keeping the log bounded so a long
// session cannot grow it without limit.
void appendLog(GuiState& state, const string& line) {
    lock_guard<mutex> snapshotLock(state.snapshotMutex);
    state.logLines.push_back(line);
    if (state.logLines.size()>500) {
        state.logLines.erase(state.logLines.begin());
    }
}
void runScan(GuiState& state) {
    const long long scanStartedAt=PlatformMetadata::monotonicNanoseconds();
    appendLog(state, "Looking for the databases of every supported agent.");
    AgentRegistry registry;
    AgentDatabaseDoctor doctor;
    const vector<DiscoveredDatabase> found=registry.discoverAllDatabases();
    {
        lock_guard<mutex> snapshotLock(state.snapshotMutex);
        state.databases.clear();
        state.selectedDatabaseIndex=-1;
        state.scanStatus="Found " + to_string(found.size()) + " databases. Diagnosing.";
    }
    int databaseIndex=0;
    for (const DiscoveredDatabase& current : found) {
        DiagnosisReport report=doctor.diagnoseDatabase(current.filePath, current.agentName, false);
        ScannedDatabase scanned(current);
        scanned.report=std::move(report);
        if (current.isSqliteDatabase && report.getPruningSupported()) {
            scanned.newestSessions=readNewestSessions(current.filePath);
        }
        {
            lock_guard<mutex> snapshotLock(state.snapshotMutex);
            state.databases.push_back(std::move(scanned));
            state.scanStatus="Diagnosed " + to_string(databaseIndex + 1) + " of "
                + to_string(found.size()) + ".";
        }
        databaseIndex++;
    }
    const long long scanFinishedAt=PlatformMetadata::monotonicNanoseconds();
    {
        lock_guard<mutex> snapshotLock(state.snapshotMutex);
        state.lastScanNanoseconds=scanFinishedAt-scanStartedAt;
        state.scanStatus="Scan complete: " + to_string(found.size()) + " databases.";
        state.scanDone=true;
    }
    appendLog(state, "Scan complete: " + to_string(found.size()) + " databases.");
    state.scanRunning=false;
}
} // namespace
void requestBackgroundScan(GuiState& state) {
    // A request while a scan is already running is dropped rather than waited
    // for. Waiting would block whichever thread asked, and that thread is the
    // one drawing the window; the current scan reports the same data anyway.
    if (state.scanRunning.load()) {
        return;
    }
    joinBackgroundScan(state);
    state.scanRunning=true;
    state.scanDone=false;
    state.scanWorker=thread(runScan, ref(state));
}
void joinBackgroundScan(GuiState& state) {
    if (state.scanWorker.joinable()) {
        state.scanWorker.join();
    }
}
}
