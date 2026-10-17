// The implementation of the action screens.
//
// The flow is the same for compaction and vacuum: preview on the worker, numbers
// in a confirmation dialogue, the word `yes` typed by a human, then the run
// on the worker, then the result dialogue. Recover shows its plan as a
// dialogue first, because a plan is meant to be read, and rebuilds only from
// there. The service view is five buttons and a result line.
// Provides `snprintf`, used to format the activity counts.
#include <cstdio>
// Provides `strlen`, used to bound the confirmation input box.
#include <cstring>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the action screens declaration.
#include "ActionScreens.hpp"
// Provides Dear ImGui: every widget on these screens.
#include <imgui.h>
// Provides the number to text conversions for the activity counts.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Gui {
namespace {
// The exact word the confirmation box demands, matching the command line.
constexpr const char* CONFIRMATION_WORD="yes";
// Opens the confirmation dialogue for a run the preview has described.
void askConfirmation(ActionUiState& ui, ActionRequest request, const string& sentence) {
    ui.confirmedRequest=request;
    ui.confirmSentence=sentence;
    ui.confirmText.clear();
    ui.confirmOpen=true;
    ImGui::OpenPopup("Confirm");
}
// Starts a request unless the worker is already running, in which case the
// click is ignored. Two actions never run at once, and the buttons disable
// themselves while one does, so this is the backstop rather than the rule.
void startWhenIdle(ActionRunner& runner, const ActionRequest& request) {
    if (!runner.isBusy()) {
        runner.start(request);
    }
}
} // namespace
void renderActivityBar(ActionRunner& runner) {
    string taskDescription;
    const long long completedUnits=runner.progress.completedUnits.load();
    const long long totalUnits=runner.progress.totalUnits.load();
    const bool isRunning=runner.progress.isRunning.load();
    {
        lock_guard<mutex> taskLock(runner.progress.taskMutex);
        taskDescription=runner.progress.taskDescription;
    }
    // The worker is the only writer of the task text, and stopping is the last
    // thing it does, so an idle worker always means an idle bar: comparing the
    // text against "Idle" left a finished task shown forever.
    if (!isRunning) {
        ImGui::TextDisabled("Idle. Nothing is running.");
        return;
    }
    ImGui::TextUnformatted(taskDescription.c_str());
    ImGui::SameLine();
    if (totalUnits>0) {
        const float fraction=static_cast<float>(
            static_cast<double>(completedUnits) / static_cast<double>(totalUnits));
        char overlayText[64]={0};
        snprintf(
            overlayText,
            sizeof(overlayText),
            "%lld of %lld",
            completedUnits,
            totalUnits);
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlayText);
    } else {
        // No total yet: the bar pulses rather than lying about a percentage.
        // Dear ImGui animates a negative fraction as an indeterminate bar.
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0), "");
    }
}
void renderDatabaseActions(
    const ScannedDatabase& scanned,
    ActionRunner& runner,
    ActionUiState& ui) {
    if (!scanned.found.isSqliteDatabase) {
        ImGui::TextDisabled(
            "This data is reported, never changed. There is nothing to click here.");
        return;
    }
    // Vacuuming rewrites any SQLite file and needs no knowledge of its schema, so
    // it is offered for every agent whose data is one. Compaction needs to know
    // which rows are redundant snapshots, which is only documented for one
    // agent; for the rest the screen says why it is not offered rather than
    // guessing at undocumented tables.
    const bool compactionSupported=scanned.report.getCompactionSupported();
    const bool workerBusy=runner.isBusy();
    ImGui::BeginDisabled(workerBusy);
    if (compactionSupported) {
        ImGui::TextUnformatted("Compaction (the event log)");
        ImGui::InputInt("Keep sessions (0 keeps the newest half)", &ui.keepSessionsInput);
        if (ui.keepSessionsInput<0) {
            ui.keepSessionsInput=0;
        }
        ImGui::InputDouble("Remove at most this share", &ui.maximumFractionInput, 0.05, 0.1, "%.2f");
        if (ui.maximumFractionInput<0.0) {
            ui.maximumFractionInput=0.0;
        }
        if (ui.maximumFractionInput>1.0) {
            ui.maximumFractionInput=1.0;
        }
        if (ImGui::Button("Preview compaction")) {
            ActionRequest request;
            request.kind=ActionKind::CompactionPreview;
            request.databasePath=scanned.found.filePath;
            request.keepSessions=ui.keepSessionsInput;
            request.maximumFraction=ui.maximumFractionInput;
            ui.lastRequest=request;
            ui.hasLastRequest=true;
            startWhenIdle(runner, request);
        }
        ImGui::SameLine();
    } else {
        ImGui::TextWrapped(
            "Compaction is not offered for this agent yet: its event log layout is not "
            "documented, so deleting rows would be a guess. The vacuum below is always safe.");
    }
    if (ImGui::Button("Preview vacuum")) {
        ActionRequest request;
        request.kind=ActionKind::VacuumPreview;
        request.databasePath=scanned.found.filePath;
        ui.lastRequest=request;
        ui.hasLastRequest=true;
        startWhenIdle(runner, request);
    }
    ImGui::SameLine();
    if (ImGui::Button("Recovery plan")) {
        ActionRequest request;
        request.kind=ActionKind::RecoverPlan;
        request.databasePath=scanned.found.filePath;
        ui.lastRequest=request;
        ui.hasLastRequest=true;
        startWhenIdle(runner, request);
    }
    ImGui::EndDisabled();
    if (workerBusy) {
        ImGui::TextDisabled("Working. The buttons return when it finishes.");
    }
}
void renderServiceView(ActionRunner& runner, ActionUiState& ui) {
    ImGui::TextUnformatted("Service");
    ImGui::Separator();
    ImGui::TextWrapped(
        "The background watcher checks every supported database once an hour and "
        "writes one line to its log when one crosses a threshold. Installing "
        "needs administrator rights.");
    const bool workerBusy=runner.isBusy();
    ImGui::BeginDisabled(workerBusy);
    // The service body itself (`run`) is deliberately not offered here: it is the
    // loop the operating system runs on the service's behalf, and it never
    // returns, so a button for it would wedge the whole interface. Watching is
    // what installing and starting means to a person using this screen.
    static const char* serviceActions[]={"status", "install", "uninstall", "start", "stop"};
    for (const char* actionName : serviceActions) {
        if (ImGui::Button(actionName)) {
            if (string(actionName)=="install" || string(actionName)=="uninstall") {
                ActionRequest request;
                request.kind=ActionKind::Service;
                request.serviceAction=string(actionName);
                askConfirmation(
                    ui,
                    request,
                    string("This will ") + actionName
                        + " the background service, which needs administrator rights. "
                          "Type yes to go ahead.");
            } else {
                ActionRequest request;
                request.kind=ActionKind::Service;
                request.serviceAction=string(actionName);
                startWhenIdle(runner, request);
            }
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndDisabled();
}
void pollActionResult(ActionRunner& runner, ActionUiState& ui) {
    ActionResult result;
    if (!runner.takeResult(result)) {
        return;
    }
    ui.lastResult=result;
    // A clean preview leads to the confirmation, carrying the exact settings
    // the preview ran with. Anything else is a result dialogue: a refusal, a
    // finished run, or a service answer.
    if (result.kind==ActionKind::CompactionPreview && result.exitCode==0 && ui.hasLastRequest) {
        ActionRequest runRequest=ui.lastRequest;
        runRequest.kind=ActionKind::CompactionRun;
        askConfirmation(
            ui,
            runRequest,
            result.summary
                + " The copy comes first and the check afterwards, exactly as the "
                  "command line does. Type yes to run it for real.");
        return;
    }
    if (result.kind==ActionKind::VacuumPreview && result.exitCode==0 && ui.hasLastRequest) {
        ActionRequest runRequest=ui.lastRequest;
        runRequest.kind=ActionKind::VacuumRun;
        askConfirmation(
            ui,
            runRequest,
            result.summary
                + " The copy comes first, exactly as the command line does. Type "
                  "yes to run it for real.");
        return;
    }
    if (result.kind==ActionKind::RecoverPlan) {
        ui.planOpen=true;
        ImGui::OpenPopup("Recovery plan");
        return;
    }
    ui.resultOpen=true;
    ImGui::OpenPopup("Result");
}
void renderActionDialogues(ActionRunner& runner, ActionUiState& ui) {
    if (ui.confirmOpen) {
        ImGui::OpenPopup("Confirm");
        ui.confirmOpen=false;
    }
    if (ImGui::BeginPopupModal("Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ui.confirmSentence.c_str());
        ImGui::TextUnformatted("Type yes to go ahead, or close this box to stop.");
        char confirmationBuffer[16]={0};
        // `snprintf` is used rather than `strncpy` because the Windows compilers
        // flag the C function as deprecated, and because it always terminates
        // the text it writes, which is what the input field needs.
        snprintf(
            confirmationBuffer,
            sizeof(confirmationBuffer),
            "%s",
            ui.confirmText.c_str());
        if (ImGui::InputText("Confirmation", confirmationBuffer, sizeof(confirmationBuffer))) {
            ui.confirmText=string(confirmationBuffer);
        }
        const bool wordMatches=ui.confirmText==CONFIRMATION_WORD;
        ImGui::BeginDisabled(!wordMatches || runner.isBusy());
        if (ImGui::Button("Go ahead")) {
            startWhenIdle(runner, ui.confirmedRequest);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ui.resultOpen) {
        ImGui::OpenPopup("Result");
        ui.resultOpen=false;
    }
    if (ImGui::BeginPopupModal("Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(ui.lastResult.title.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", ui.lastResult.summary.c_str());
        for (const string& detailLine : ui.lastResult.detailLines) {
            ImGui::BulletText("%s", detailLine.c_str());
        }
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ui.planOpen) {
        ImGui::OpenPopup("Recovery plan");
        ui.planOpen=false;
    }
    if (ImGui::BeginPopupModal(
            "Recovery plan", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ui.lastResult.summary.c_str());
        ImGui::Separator();
        for (const auto& planStep : ui.lastResult.planSteps) {
            ImGui::BulletText(
                "%s%s", planStep.title().c_str(), planStep.isDestructive() ? " (changes data)" : "");
            ImGui::TextWrapped("%s", planStep.explanation().c_str());
            if (!planStep.command().empty()) {
                ImGui::TextDisabled("%s", planStep.command().c_str());
            }
        }
        ImGui::Separator();
        ImGui::BeginDisabled(runner.isBusy());
        if (ImGui::Button("Rebuild a working copy")) {
            if (ui.hasLastRequest) {
                ActionRequest rebuildRequest=ui.lastRequest;
                rebuildRequest.kind=ActionKind::RecoverRebuild;
                ImGui::CloseCurrentPopup();
                askConfirmation(
                    ui,
                    rebuildRequest,
                    "This builds a working copy holding every row that can still be "
                    "read. The original is never modified. Type yes to go ahead.");
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
}
