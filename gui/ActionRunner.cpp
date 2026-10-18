// The action worker: one command per request, on its own thread.
//
// Each request builds its command locally, points the command's progress sink
// at the shared numbers, and runs it with explicit settings: the database
// path is always set, the run is always quiet (the dialogue shows the outcome
// structs, not the console text), and confirmation is always pre-answered,
// because the interface asked the user itself before starting the worker.
// Provides `exception`, the standard failure type the worker catches first.
#include <exception>
// Provides `string`, used for the sentences the dialogues show.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the action state this worker publishes to.
#include "ActionState.hpp"
// Provides the four command classes the worker runs.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
#include "AgentDbDoctor/Command/RecoverCommand.hpp"
#include "AgentDbDoctor/Command/ServiceCommand.hpp"
// Provides the command settings value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the number to text conversions for the result lines.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Gui {
namespace {
// Points a request's sink at the shared numbers and names the task, so even
// a command with no bar of its own shows a live activity line.
CommandOptions workerOptions(const ActionRequest& request, ActionRunner* runner) {
    CommandOptions options;
    options.databaseFilePath=request.databasePath;
    options.answerYesToEverything=true;
    options.quiet=true;
    options.sessionsToKeep=request.keepSessions;
    options.maximumFractionToRemove=request.maximumFraction;
    options.serviceAction=request.serviceAction;
    ActionRunner* runnerPointer=runner;
    options.progressSink=[runnerPointer](const ProgressUpdate& update) {
        runnerPointer->progress.completedUnits=update.completedUnits;
        runnerPointer->progress.totalUnits=update.totalUnits;
        {
            lock_guard<mutex> taskLock(runnerPointer->progress.taskMutex);
            runnerPointer->progress.taskDescription=update.taskDescription;
        }
        if (update.isFinished) {
            runnerPointer->progress.isFinished=true;
        }
    };
    return options;
}
// Names the activity before a command that carries no bar of its own, so the
// bar is never blank while real work happens underneath it.
void nameTask(ActionRunner* runner, const string& taskDescription) {
    lock_guard<mutex> taskLock(runner->progress.taskMutex);
    runner->progress.taskDescription=taskDescription;
}
void runCompactionPreview(ActionRunner* runner, ActionRequest request, ActionResult& result) {
    nameTask(runner, "Previewing the compaction");
    CommandOptions options=workerOptions(request, runner);
    options.dryRunOnly=true;
    CompactCommand command(false);
    result.exitCode=command.execute(options);
    const EventLogCompactionReport& outcome=command.lastOutcome();
    result.kind=ActionKind::CompactionPreview;
    result.title="Compaction preview";
    result.summary=outcome.summarySentence;
    if (outcome.reclaimableBytesNow>0) {
        result.detailLines.push_back(
            string("Space the following vacuum would hand back: ")
            + HumanReadableFormatting::formatBytes(outcome.reclaimableBytesNow));
    }
    result.detailLines.push_back(
        string("Sessions kept untouched: ") + to_string(outcome.protectedSessionCount));
    result.detailLines.push_back(
        string("Sessions checked afterwards: ") + to_string(outcome.verifiedSessionCount));
}
void runCompactionRun(ActionRunner* runner, ActionRequest request, ActionResult& result) {
    CommandOptions options=workerOptions(request, runner);
    CompactCommand command(false);
    result.exitCode=command.execute(options);
    const EventLogCompactionReport& outcome=command.lastOutcome();
    result.kind=ActionKind::CompactionRun;
    result.title="Compaction finished";
    result.summary=outcome.summarySentence;
    result.detailLines.push_back(
        string("Rows removed: ") + HumanReadableFormatting::formatCount(outcome.removedRowCount));
    if (outcome.safetyCopyWasRemoved) {
        result.detailLines.push_back(
            string("Safety copy removed after the checks passed: ")
            + HumanReadableFormatting::formatBytes(outcome.safetyCopyBytes) + " handed back.");
    }
    if (outcome.wasRolledBack) {
        result.detailLines.push_back(
            "The check after the removal did not pass, so the safety copy was put back and the "
            "database was checked again. Nothing was lost.");
    }
}
void runVacuumPreview(ActionRunner* runner, ActionRequest request, ActionResult& result) {
    nameTask(runner, "Previewing the vacuum");
    CommandOptions options=workerOptions(request, runner);
    options.dryRunOnly=true;
    VacuumCommand command(false);
    result.exitCode=command.execute(options);
    const VacuumOutcome& outcome=command.lastOutcome();
    result.kind=ActionKind::VacuumPreview;
    result.title="Vacuum preview";
    result.summary=outcome.summarySentence;
    result.detailLines.push_back(
        string("Size now: ") + HumanReadableFormatting::formatBytes(outcome.sizeBeforeBytes));
    if (outcome.bytesReclaimed>0) {
        result.detailLines.push_back(
            string("Would hand back: ")
            + HumanReadableFormatting::formatBytes(outcome.bytesReclaimed));
    }
}
void runVacuumRun(ActionRunner* runner, ActionRequest request, ActionResult& result) {
    CommandOptions options=workerOptions(request, runner);
    VacuumCommand command(false);
    result.exitCode=command.execute(options);
    const VacuumOutcome& outcome=command.lastOutcome();
    result.kind=ActionKind::VacuumRun;
    result.title="Vacuum finished";
    result.summary=outcome.summarySentence;
    result.detailLines.push_back(
        string("Size before: ") + HumanReadableFormatting::formatBytes(outcome.sizeBeforeBytes));
    result.detailLines.push_back(
        string("Size after: ") + HumanReadableFormatting::formatBytes(outcome.sizeAfterBytes));
    if (outcome.keptSafetyCopyBytes>0) {
        result.detailLines.push_back(
            string("Safety copy kept: ") + HumanReadableFormatting::formatBytes(outcome.keptSafetyCopyBytes));
        const long long netBytes=outcome.netBytesFreed;
        result.detailLines.push_back(
            string("Net free space change: ")
            + (netBytes>=0 ? string("+") : string("-"))
            + HumanReadableFormatting::formatBytes(netBytes>=0 ? netBytes : -netBytes));
    } else if (outcome.wasCompacted) {
        result.detailLines.push_back(
            "The safety copy was removed after the checks passed, so the space it held came back too.");
    }
}
void runRecoverPlan(ActionRunner* runner, ActionRequest request, ActionResult& result) {
    nameTask(runner, "Reading the recovery plan");
    CommandOptions options=workerOptions(request, runner);
    RecoverCommand command(false);
    result.planSteps=command.buildPlanFor(options);
    result.exitCode=0;
    result.kind=ActionKind::RecoverPlan;
    result.title="Recovery plan";
    result.summary=command.lastOutcome().summarySentence.empty()
        ? string(
            "The plan above is what the tool found. Nothing has been changed, and "
            "nothing changes until a rebuild is confirmed.")
        : command.lastOutcome().summarySentence;
}
void runRecoverRebuild(ActionRunner* runner, ActionRequest request, ActionResult& result) {
    CommandOptions options=workerOptions(request, runner);
    options.rebuildWorkingCopy=true;
    RecoverCommand command(false);
    result.exitCode=command.execute(options);
    const RecoverOutcome& outcome=command.lastOutcome();
    result.kind=ActionKind::RecoverRebuild;
    result.title="Recovery rebuild finished";
    result.summary=outcome.summarySentence;
    if (!outcome.rebuiltDatabasePath.empty()) {
        result.detailLines.push_back(string("Working copy: ") + outcome.rebuiltDatabasePath);
    }
    if (outcome.recoveredRowCount>0) {
        result.detailLines.push_back(
            string("Rows recovered: ")
            + HumanReadableFormatting::formatCount(outcome.recoveredRowCount));
    }
}
void runService(ActionRunner* runner, ActionRequest request, ActionResult& result) {
    nameTask(runner, string("Service: ") + request.serviceAction);
    CommandOptions options=workerOptions(request, runner);
    ServiceCommand command(false);
    result.exitCode=command.execute(options);
    result.kind=ActionKind::Service;
    result.title=string("Service ") + request.serviceAction;
    result.summary=result.exitCode==0 ? string("Done. The command reported success.")
                                      : string("The command reported a problem. Run it in a "
                                          "terminal to see the full explanation.");
}
void runRequest(ActionRunner* runner, ActionRequest request) {
    runner->progress.isRunning=true;
    runner->progress.isFinished=false;
    runner->progress.completedUnits=0;
    runner->progress.totalUnits=0;
    ActionResult result;
    result.kind=request.kind;
    try {
        switch (request.kind) {
            case ActionKind::CompactionPreview:
                runCompactionPreview(runner, request, result);
                break;
            case ActionKind::CompactionRun:
                runCompactionRun(runner, request, result);
                break;
            case ActionKind::VacuumPreview:
                runVacuumPreview(runner, request, result);
                break;
            case ActionKind::VacuumRun:
                runVacuumRun(runner, request, result);
                break;
            case ActionKind::RecoverPlan:
                runRecoverPlan(runner, request, result);
                break;
            case ActionKind::RecoverRebuild:
                runRecoverRebuild(runner, request, result);
                break;
            case ActionKind::Service:
                runService(runner, request, result);
                break;
            case ActionKind::None:
                break;
        }
    } catch (const exception& operationFailure) {
        // Nothing may escape a worker thread: an escaping exception ends the whole
        // process, dialogue and all. The command classes catch their own known
        // failures and return codes, so this is the backstop for the rest.
        result.exitCode=1;
        result.title="Something went wrong";
        result.summary=string("The operation did not finish. ") + operationFailure.what()
            + "\n\nYour original data was not replaced. To read the full explanation, run the "
              "same command in a terminal.";
    } catch (...) {
        // The tool's own failure types deliberately share no common base class, so
        // this backstop cannot read their sentences. It still keeps one failed
        // action from ending the whole program.
        result.exitCode=1;
        result.title="Something went wrong";
        result.summary=string("The operation did not finish, and your original data was not "
            "replaced.\n\nTo read the full explanation, run the same command in a terminal "
            "and it will print the reason there.");
    }
    // The result is stored before the worker is marked idle, so any frame that
    // reads an idle worker through `isBusy` also finds its result already waiting.
    // The numbers were final before that, so the bar never jumps backwards on the
    // frame the dialogue appears.
    runner->storeResult(result);
    runner->progress.isFinished=true;
    runner->progress.isRunning=false;
}
} // namespace
ActionRunner::~ActionRunner() {
    join();
}
void ActionRunner::start(const ActionRequest& request) {
    join();
    progress.isRunning=true;
    progress.isFinished=false;
    progress.completedUnits=0;
    progress.totalUnits=0;
    {
        lock_guard<mutex> taskLock(progress.taskMutex);
        progress.taskDescription="Starting";
    }
    {
        lock_guard<mutex> finishedLock(resultMutex);
        resultWaiting=false;
    }
    actionWorker=thread(runRequest, this, request);
}
bool ActionRunner::isBusy() const {
    return progress.isRunning;
}
void ActionRunner::join() {
    if (actionWorker.joinable()) {
        actionWorker.join();
    }
}
void ActionRunner::storeResult(const ActionResult& result) {
    lock_guard<mutex> finishedLock(resultMutex);
    finishedResult=result;
    resultWaiting=true;
}
bool ActionRunner::takeResult(ActionResult& resultOut) {
    lock_guard<mutex> finishedLock(resultMutex);
    if (!resultWaiting) {
        return false;
    }
    resultOut=finishedResult;
    resultWaiting=false;
    return true;
}
}
