// The action center state: what the user asked to do, how far it has got,
// and what it found.
//
// Destructive work runs on a worker thread so the interface keeps drawing at
// full frame rate. The worker owns its command objects locally and publishes
// through atomics: numbers for the activity bar, and one result value, taken
// exactly once, for the result dialogue. The interface thread never touches
// a database.
#ifndef AGENT_DB_DOCTOR_GUI_ACTIONSTATE_HPP
#define AGENT_DB_DOCTOR_GUI_ACTIONSTATE_HPP
// Provides `atomic`, for the numbers the loop reads without locking.
#include <atomic>
// Provides `mutex`, guarding the text and the result value.
#include <mutex>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `thread`, which runs one action at a time.
#include <thread>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the recovery plan steps the recover screen renders verbatim.
#include "AgentDbDoctor/Recovery/RecoveryRecommendation.hpp"
using namespace std;
namespace AgentDbDoctor::Gui {
// What the worker is doing, or was asked to do.
enum class ActionKind {
    // Nothing running and nothing asked for.
    None,
    // A compaction that changes nothing, to learn the numbers for the confirmation.
    CompactionPreview,
    // A compaction that removes rows, after the typed confirmation.
    CompactionRun,
    // A compaction that changes nothing, to learn the numbers.
    VacuumPreview,
    // A compaction that rewrites the file, after the typed confirmation.
    VacuumRun,
    // A recovery plan that runs nothing.
    RecoverPlan,
    // A recovery rebuild, after the typed confirmation.
    RecoverRebuild,
    // One service action: install, uninstall, start, stop, or status.
    Service
};
// One request to the worker: the kind, the database, and the settings.
struct ActionRequest final {
    // What to do.
    ActionKind kind=ActionKind::None;
    // The database file, always set explicitly by the interface.
    string databasePath;
    // How many recent sessions a compaction keeps. Zero means the tool's default.
    int keepSessions=0;
    // The largest share of the event table a compaction may remove.
    double maximumFraction=0.5;
    // The service action, for `ActionKind::Service` only.
    string serviceAction;
};
// The live numbers for the activity bar, written by the worker through the
// progress sink and read by the loop once per frame without locking.
struct ActionProgress final {
    // Units done and units total, in the command's own units.
    atomic<long long> completedUnits{0};
    atomic<long long> totalUnits{0};
    // The lock for the task description, which changes rarely.
    mutex taskMutex;
    // What the work is, for example "Removing older copies".
    string taskDescription="Idle";
    // True while the worker runs.
    atomic<bool> isRunning{false};
    // True when the final report arrived.
    atomic<bool> isFinished{false};
};
// What an action found, taken once by the interface for its dialogue.
struct ActionResult final {
    // What was done.
    ActionKind kind=ActionKind::None;
    // The process exit code the command returned.
    int exitCode=0;
    // The dialogue title.
    string title;
    // The closing sentence.
    string summary;
    // One line per number that matters.
    vector<string> detailLines;
    // The recovery steps, for `RecoverPlan` only, rendered verbatim.
    vector<AgentDbDoctor::Recovery::RecoveryRecommendation> planSteps;
};
// Runs one action at a time on a worker thread.
//
// The class holds its thread and joins it on destruction and before every new
// request, so a worker never outlives the state it writes to and two actions
// never run at once.
class ActionRunner final {
public:
    ActionRunner()=default;
    ~ActionRunner();
    ActionRunner(const ActionRunner&)=delete;
    ActionRunner& operator=(const ActionRunner&)=delete;
    // Starts the request on the worker, waiting for any previous one first.
    void start(const ActionRequest& request);
    // True while the worker runs.
    bool isBusy() const;
    // Waits for the worker. Called before a new request and on shutdown.
    void join();
    // Takes the finished result, once. Returns false when there is none.
    bool takeResult(ActionResult& resultOut);
    // The live numbers for the activity bar.
    ActionProgress progress;
    // Stores a finished result for the interface to take. Called by the
    // worker, which is the only writer, so each result is seen exactly once.
    void storeResult(const ActionResult& result);
private:
    // The worker thread, joinable only while an action runs.
    thread actionWorker;
    // The lock for the result value below.
    mutable mutex resultMutex;
    // True when a result is waiting to be taken.
    bool resultWaiting=false;
    // The finished result.
    ActionResult finishedResult;
};
}
#endif
