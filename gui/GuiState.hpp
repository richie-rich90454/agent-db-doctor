// The shared state between the interface thread and the background worker.
//
// Scanning twenty one agents and diagnosing what they hold takes longer than
// one frame, so it runs on a worker thread. This header holds the snapshot
// the worker fills in and the loop reads: plain data under one mutex, plus
// two atomics for the questions the loop asks every frame. Nothing here draws
// anything; drawing lives in the dashboard screen.
#ifndef AGENT_DB_DOCTOR_GUI_GUISTATE_HPP
#define AGENT_DB_DOCTOR_GUI_GUISTATE_HPP
// Provides `atomic`, for the flags the loop reads without locking.
#include <atomic>
// Provides `mutex`, the one lock guarding the snapshot.
#include <mutex>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `thread`, which runs the background scan.
#include <thread>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DiscoveredDatabase value type the scan reports.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides the DiagnosisReport value type each scan produces.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
using namespace std;
namespace AgentDbDoctor::Gui {
// One row of the session browser: what the session is called and when it last
// changed. Columns beyond these are schema specific and therefore not read.
struct SessionRow final {
    // The session identifier, the only column guaranteed to exist.
    string sessionIdentifier;
    // The title, or the identifier again when the table has no title column.
    string sessionTitle;
    // The creation or update time as stored, or zero when unknown.
    long long epochSeconds=0;
};
// One scanned database: what was found, what the diagnosis said, and the
// newest sessions for the browser. SQLite databases that are not the
// compactable agent's carry an empty session list, because their tables are
// undocumented and guessing column names would be exactly that.
struct ScannedDatabase final {
    // Where the data was found and which agent owns it.
    AgentDbDoctor::Agents::DiscoveredDatabase found;
    // The full diagnosis report for the detail view.
    AgentDbDoctor::Doctor::DiagnosisReport report;
    // The newest sessions, newest first, capped at the browser limit.
    vector<SessionRow> newestSessions;
    // Reports need a path at construction, so a scanned entry is built from
    // the discovery result directly rather than default constructed.
    explicit ScannedDatabase(const AgentDbDoctor::Agents::DiscoveredDatabase& discovered)
        : found(discovered),
          report(discovered.filePath) {
    }
};
// Everything the worker thread publishes and the interface thread reads.
struct GuiState final {
    // The one lock. Held briefly to swap the snapshot or append a log line,
    // never across a database call.
    mutex snapshotMutex;
    // The worker thread. Joinable only while a scan is running.
    thread scanWorker;
    // True while the worker is running. Read by the loop without locking.
    atomic<bool> scanRunning=false;
    // True once a scan has completed. Read by the loop without locking.
    atomic<bool> scanDone=false;
    // One line describing the scan: waiting, running, or finished with counts.
    // Written under the lock, copied out by the loop.
    string scanStatus="Waiting to scan.";
    // The databases of the last completed scan, in discovery order.
    vector<ScannedDatabase> databases;
    // The console lines, newest last, capped by the dashboard screen.
    vector<string> logLines;
    // The selected agent filter, or empty for every agent.
    string selectedAgentName;
    // The selected database index into `databases`, or -1 for none.
    int selectedDatabaseIndex=-1;
    // How long the last scan took, in nanoseconds, for the status bar.
    long long lastScanNanoseconds=0;
};
// Starts a background scan, joining any previous worker first. Safe to call
// for a rescan: the old results stay visible until the new ones arrive.
void requestBackgroundScan(GuiState& state);
// Waits for the worker to finish. Called on rescan and on shutdown, so no
// thread ever outlives the state it writes to.
void joinBackgroundScan(GuiState& state);
}
#endif
