// The implementation of the screens.
//
// The file is laid out the way the window is: the request builders and the run
// handling first, because every button depends on them, then the five tabs in the
// order they appear, then the activity bar and the log pane that are always there,
// and then the dialogues.
//
// Every command is described to the user as the command it is. Each action button
// carries the exact command line it would run as its tooltip, because that is the
// whole design of this window: it does not do anything the command line cannot do,
// and a user who wants to know what a button really does can read it there.
// Provides `sort`, which orders the database table and the overview list.
#include <algorithm>
// Provides `snprintf`, for the progress overlay.
#include <cstdio>
// Provides `FLT_MAX`, the unbounded side of a dialogue's size constraints.
#include <cfloat>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the screens declaration.
#include "AppScreens.hpp"
// Provides Dear ImGui: every widget on these screens.
#include <imgui.h>
using namespace std;
namespace AgentDbDoctor::Gui {
namespace {
// The exact word the confirmation box demands, matching the command line.
constexpr const char* CONFIRMATION_WORD="yes";
// The heights of the fixed parts of the layout. The tables scroll inside
// themselves, so a long list never pushes the rest of the tab off the screen.
constexpr float DATABASE_TABLE_HEIGHT=260.0f;
constexpr float SESSION_TABLE_HEIGHT=190.0f;
constexpr float SPACE_TABLE_HEIGHT=320.0f;
constexpr float PLAN_BODY_HEIGHT=420.0f;
constexpr float DIALOG_BODY_HEIGHT=170.0f;
constexpr float ABOUT_AGENT_LIST_HEIGHT=260.0f;
// The width a fixed table column needs for a piece of text at the current font.
//
// The columns used to be fixed pixel counts, which clipped their headers and their
// buttons the moment the display scaled the font up: "Reclaimable" became
// "Reclaim..." and a Reclaim button lost its last letter at the table's edge. A
// width computed from the text is right at every scale.
float columnWidthForText(const char* columnText) {
    return ImGui::CalcTextSize(columnText).x + ImGui::GetStyle().CellPadding.x * 2.0f + 12.0f;
}
// The colours the window uses for health, so one look at a table tells a reader
// what needs attention. The words beside them say the same thing, because colour
// alone is not a message.
ImVec4 healthyColour() {
    return ImVec4(0.22f, 0.68f, 0.32f, 1.0f);
}
ImVec4 attentionColour() {
    return ImVec4(0.88f, 0.62f, 0.15f, 1.0f);
}
ImVec4 problemColour() {
    return ImVec4(0.85f, 0.28f, 0.22f, 1.0f);
}
ImVec4 quietColour() {
    return ImVec4(0.55f, 0.55f, 0.58f, 1.0f);
}
// The colour of a health word.
ImVec4 healthColour(const DatabaseRecord& record) {
    if (record.isDirectory || !record.exists) {
        return quietColour();
    }
    if (record.isHealthy) {
        return healthyColour();
    }
    if (record.integrity=="damaged" || record.integrity=="unreadable") {
        return problemColour();
    }
    return attentionColour();
}
// True when the command line could open the database, which is the one honest test
// for whether compacting or vacuuming it can work at all. A folder of text files
// and a file that is not a database have nothing to rewrite.
bool acceptsDestructiveActions(const DatabaseRecord& record) {
    return record.exists && !record.isDirectory && record.databaseOpenSucceeded;
}
// The last two fragments of a path, so a table row fits on one line. The whole path
// is in the tooltip and in the drawer.
string shortPath(const string& filePath) {
    const size_t lastSeparator=filePath.find_last_of("/\\");
    if (lastSeparator==string::npos) {
        return filePath;
    }
    const size_t beforeLast=filePath.find_last_of("/\\", lastSeparator==0 ? 0 : lastSeparator - 1);
    if (beforeLast==string::npos) {
        return filePath;
    }
    return string("...") + filePath.substr(beforeLast);
}
// True when the text holds the fragment, compared without regard to case.
bool containsIgnoringCase(const string& text, const string& fragment) {
    if (fragment.empty()) {
        return true;
    }
    if (text.size()<fragment.size()) {
        return false;
    }
    for (size_t startIndex=0; startIndex + fragment.size()<=text.size(); ++startIndex) {
        bool matchesHere=true;
        for (size_t fragmentIndex=0; fragmentIndex<fragment.size(); ++fragmentIndex) {
            char textCharacter=text[startIndex + fragmentIndex];
            if (textCharacter>='A' && textCharacter<='Z') {
                textCharacter=static_cast<char>(textCharacter - 'A' + 'a');
            }
            char fragmentCharacter=fragment[fragmentIndex];
            if (fragmentCharacter>='A' && fragmentCharacter<='Z') {
                fragmentCharacter=static_cast<char>(fragmentCharacter - 'A' + 'a');
            }
            if (textCharacter!=fragmentCharacter) {
                matchesHere=false;
                break;
            }
        }
        if (matchesHere) {
            return true;
        }
    }
    return false;
}
// True when the record matches the filter box, which matches on the agent name and
// on the path.
bool matchesFilter(const DatabaseRecord& record, const string& filterText) {
    if (filterText.empty()) {
        return true;
    }
    return containsIgnoringCase(record.agentName, filterText)
        || containsIgnoringCase(record.filePath, filterText);
}
// The command line a request would run, as text for a tooltip.
string commandLineText(const CliInvocation& invocation) {
    const string locatedProgram=CliRunner::locateCliExecutable();
    string commandText=locatedProgram.empty() ? string("agent-db-doctor") : locatedProgram;
    for (const string& argument : buildCommandArguments(invocation)) {
        commandText+=" ";
        // An argument with a space in it is quoted, so the line reads as something
        // that could be pasted into a terminal.
        if (argument.find(' ')!=string::npos) {
            commandText+="\"" + argument + "\"";
        } else {
            commandText+=argument;
        }
    }
    return commandText;
}
// Shows a tooltip carrying the command line of the button just drawn, plus one
// sentence saying what that command does.
void showCommandTooltip(const CliInvocation& invocation, const string& explanation) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    const string commandText=commandLineText(invocation);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
    ImGui::TextUnformatted(explanation.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted(commandText.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
// Shows a tooltip of one sentence, for a control that runs nothing.
void showExplanationTooltip(const string& explanation) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
    ImGui::TextUnformatted(explanation.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
// A labelled value on one line of the drawer's metric table. The label column is
// given a fixed width and the value column takes the rest, because a value that
// wraps needs a column wide enough to wrap inside: a table sized to its content
// would give a wrapped value one character per line.
void metricRow(const char* label, const string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.c_str());
}
// A block of plain lines, scrollable, with a fixed height. Used wherever a screen or
// a dialogue has to show what a command printed. The horizontal scrollbar is not a
// decoration: a command's output contains file paths, and a path that runs past the
// edge of the block would otherwise be unreadable rather than scrollable.
void textLineBlock(const char* childName, const vector<string>& lines, float blockHeight) {
    if (ImGui::BeginChild(
            childName,
            ImVec2(0, blockHeight),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const string& line : lines) {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::EndChild();
}
// The sentence the service tab shows about the state of the background watcher.
string serviceStateText(const ServiceStatus& service) {
    if (!service.isAvailable) {
        return string("The state has not been asked for yet. Press Check status.");
    }
    if (!service.isInstalled) {
        return string("Not installed, so nothing is being watched in the background.");
    }
    if (service.isRunning) {
        return string("Installed and running.");
    }
    return string("Installed, but not running.");
}
// The reason a database cannot be changed, or the empty string when it can.
string refusalText(const DatabaseRecord& record) {
    if (!record.exists) {
        return string("There is no file at this path, so there is nothing to act on.");
    }
    if (record.isDirectory) {
        return string(
            "This is a folder of text files rather than a database, so the tool reports it and "
            "never changes it.");
    }
    if (!record.databaseOpenSucceeded) {
        return string(
            "The command could not open this database, so compacting or vacuuming it would fail. "
            "The recovery plan is the step that can help.");
    }
    return string();
}
// The invocations the actions use, built in one place so that a button and the run
// that follows its preview cannot disagree about an argument.
CliInvocation compactionInvocation(const string& databasePath, bool dryRun) {
    CliInvocation invocation;
    invocation.command="compact";
    invocation.databasePath=databasePath;
    invocation.dryRun=dryRun;
    invocation.answerYes=!dryRun;
    return invocation;
}
CliInvocation vacuumInvocation(const string& databasePath, bool dryRun) {
    CliInvocation invocation;
    invocation.command="vacuum";
    invocation.databasePath=databasePath;
    invocation.dryRun=dryRun;
    invocation.answerYes=!dryRun;
    return invocation;
}
CliInvocation reclaimInvocation(const string& databasePath, long long targetBytes, bool dryRun) {
    CliInvocation invocation;
    invocation.command="reclaim";
    invocation.databasePath=databasePath;
    invocation.reclaimTargetBytes=targetBytes;
    invocation.dryRun=dryRun;
    invocation.answerYes=!dryRun;
    return invocation;
}
CliInvocation recoverPlanInvocation(const string& databasePath) {
    CliInvocation invocation;
    invocation.command="recover";
    invocation.databasePath=databasePath;
    return invocation;
}
CliInvocation recoverRebuildInvocation(const string& databasePath) {
    CliInvocation invocation=recoverPlanInvocation(databasePath);
    invocation.rebuildWorkingCopy=true;
    invocation.answerYes=true;
    return invocation;
}
CliInvocation backupsListInvocation(const string& databasePath) {
    CliInvocation invocation;
    invocation.command="backups";
    invocation.action="list";
    invocation.databasePath=databasePath;
    return invocation;
}
CliInvocation sessionsListInvocation(const string& databasePath) {
    CliInvocation invocation;
    invocation.command="sessions";
    invocation.action="list";
    invocation.databasePath=databasePath;
    invocation.sessionLimit=50;
    return invocation;
}
CliInvocation diagnoseInvocation(const string& databasePath, bool integrityCheck) {
    CliInvocation invocation;
    invocation.command="diagnose";
    invocation.databasePath=databasePath;
    invocation.integrityCheck=integrityCheck;
    return invocation;
}
CliInvocation spaceInvocation(const string& databasePath) {
    CliInvocation invocation;
    invocation.command="explain-space";
    invocation.databasePath=databasePath;
    return invocation;
}
CliInvocation exportInvocation(
    const string& databasePath,
    const string& sessionIdentifier,
    const string& exportFormat,
    const string& outputPath) {
    CliInvocation invocation;
    invocation.command="export";
    invocation.databasePath=databasePath;
    invocation.sessionIdentifier=sessionIdentifier;
    invocation.exportFormat=exportFormat;
    invocation.outputPath=outputPath;
    return invocation;
}
CliInvocation sessionDeleteInvocation(const string& databasePath, const string& sessionIdentifier) {
    CliInvocation invocation;
    invocation.command="sessions";
    invocation.action="delete";
    invocation.databasePath=databasePath;
    invocation.sessionIdentifier=sessionIdentifier;
    invocation.answerYes=true;
    return invocation;
}
CliInvocation serviceActionInvocation(const string& action) {
    CliInvocation invocation;
    invocation.command="service";
    invocation.action=action;
    return invocation;
}
// Adds the safety copy settings every destructive run shares: where the copy goes
// when the user named a place, and whether it is kept afterwards.
void addBackupOptions(CliInvocation& invocation, const InterfaceSettings& settings) {
    invocation.backupDestinationPath=settings.backupDestinationPath;
    invocation.keepSafetyCopy=settings.keepSafetyCopy;
}
// The namespace function that follows the drawer. Declared here because the drawer
// above it calls it.
void renderSessionSection(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    const DatabaseRecord& record);
// Fills in the dialogue copy of a finished run and opens whatever its purpose calls
// for. Called once, from pollRunner, in the frame the child ended.
void completeRun(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    int exitCode,
    bool wasCancelled);
// The five tabs.
void renderOverviewTab(InterfaceState& ui, GuiState& state, CliRunner& runner);
void renderDatabasesTab(InterfaceState& ui, GuiState& state, CliRunner& runner);
void renderHistoryTab(InterfaceState& ui, GuiState& state, CliRunner& runner);
void renderServiceTab(InterfaceState& ui, GuiState& state, CliRunner& runner);
void renderSettingsTab(InterfaceState& ui, GuiState& state);
} // namespace
RunRequest scanRequest() {
    RunRequest request;
    request.purpose=RunPurpose::Scan;
    request.invocation.command="diagnose";
    request.description="Looking at every database";
    request.resultTitle="The look finished";
    return request;
}
RunRequest agentsRequest() {
    RunRequest request;
    request.purpose=RunPurpose::AgentsList;
    request.invocation.command="agents";
    request.invocation.verifyCandidatePaths=true;
    request.description="Asking which agents are supported";
    request.resultTitle="The agents this tool supports";
    return request;
}
RunRequest databaseDiagnoseRequest(const string& databasePath, bool integrityCheck) {
    RunRequest request;
    request.purpose=RunPurpose::DatabaseDiagnose;
    request.invocation=diagnoseInvocation(databasePath, integrityCheck);
    request.description=integrityCheck
        ? string("Checking every page of one database")
        : string("Looking at one database");
    request.resultTitle=integrityCheck ? string("Integrity check finished")
                                      : string("The look finished");
    return request;
}
RunRequest sessionsListRequest(const string& databasePath, FollowUp followUp) {
    RunRequest request;
    request.purpose=RunPurpose::SessionsList;
    request.invocation=sessionsListInvocation(databasePath);
    request.description="Reading the largest conversations";
    request.resultTitle="The conversation list";
    request.followUp=followUp;
    return request;
}
RunRequest historyRefreshRequest() {
    RunRequest request;
    request.purpose=RunPurpose::HistoryRefresh;
    request.invocation.command="history";
    request.description="Reading the recorded sizes";
    request.resultTitle="The recorded sizes";
    return request;
}
RunRequest serviceStatusRequest() {
    RunRequest request;
    request.purpose=RunPurpose::ServiceCheck;
    request.invocation=serviceActionInvocation("status");
    request.description="Asking what the service is doing";
    request.resultTitle="The service status";
    return request;
}
void startRun(InterfaceState& ui, GuiState& state, CliRunner& runner, const RunRequest& request) {
    if (runner.isRunning() || request.purpose==RunPurpose::None
        || request.invocation.command.empty()) {
        return;
    }
    // What the run publishes replaces what the previous one published. A scan also
    // empties the database list, because it is about to describe every database
    // afresh; anything else keeps the records, since they describe the machine
    // rather than the run.
    if (request.purpose==RunPurpose::Scan) {
        beginScan(state);
    } else {
        beginRun(state, request.description);
    }
    if (request.purpose==RunPurpose::SessionsList) {
        // A conversation list belongs to the database it was asked for, so the old
        // entries go before the new ones arrive.
        prepareSessionList(state, request.invocation.databasePath);
    }
    if (request.purpose==RunPurpose::HistoryRefresh) {
        beginHistoryRefresh(state);
    }
    ui.currentRun=request;
    ui.runWasCancelled=false;
    if (!runner.start(buildCommandArguments(request.invocation))) {
        // The runner has already queued the reason as a line for the log pane. The
        // dialogue reads the state, so the same facts go there too.
        state.hasError=true;
        state.lastErrorMessage=string("The command line program could not be started. ")
            + CliRunner::locateCliExecutableDetails();
        state.lastErrorCode.clear();
    }
}
void openConfirmation(
    InterfaceState& ui,
    const RunRequest& runRequest,
    const string& sentence,
    const vector<string>& previewLines) {
    ui.confirmedRun=runRequest;
    ui.confirmationSentence=sentence;
    ui.confirmationPreviewLines=previewLines;
    // The box starts empty, every time: a remembered word that still matched would
    // turn a confirmation into a button press.
    ui.confirmationInput[0]='\0';
    ui.confirmationOpen=true;
}
void selectTab(InterfaceState& ui, InterfaceTab tab) {
    if (ui.currentTab==tab) {
        return;
    }
    ui.currentTab=tab;
    // The tab bar is asked to move on the next frame, because it owns its own
    // selection: a menu item cannot change a tab bar's mind by setting a variable.
    ui.tabChangeRequested=true;
    if (tab==InterfaceTab::History) {
        // The history is read when the tab is opened, which is when somebody wants
        // to see it. The refresh button does the same thing on demand.
        ui.historyRefreshRequested=true;
    }
}
void pollRunner(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    // Every line the child produced since the last frame: the log pane gets the
    // text, and the lines that are JSON objects go on to the state.
    const vector<CliLine> lines=runner.takeLines();
    for (const CliLine& line : lines) {
        appendLogLine(state, line.text);
        if (line.isJson) {
            applyJsonEvent(state, line.json);
        }
    }
    if (!state.run.isActive || runner.isRunning()) {
        return;
    }
    // The child has ended and every line it wrote has been read, so the run can be
    // closed. The exit code the command line reports in its last object is the one
    // used, and the operating system's number for the process is only the fallback
    // for a run that wrote no objects at all: the window starts every command in the
    // background so that no console window appears behind it, and SDL reports zero
    // for the exit code of a background process on every platform.
    runner.join();
    const int exitCode=state.hasExitCode ? state.lastExitCode : runner.exitCode();
    const bool wasCancelled=ui.runWasCancelled;
    endRun(state, exitCode);
    completeRun(ui, state, runner, exitCode, wasCancelled);
}
namespace {
void completeRun(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    int exitCode,
    bool wasCancelled) {
    const RunRequest finishedRun=ui.currentRun;
    CompletedRun completed;
    completed.isAvailable=true;
    completed.purpose=finishedRun.purpose;
    completed.title=finishedRun.resultTitle.empty() ? string("Result") : finishedRun.resultTitle;
    completed.exitCode=exitCode;
    completed.wasCancelled=wasCancelled;
    completed.verdict=runVerdictSentence(state, exitCode, wasCancelled);
    completed.databasePath=finishedRun.invocation.databasePath;
    completed.fields=state.lastResult.fields;
    completed.hasError=state.hasError;
    completed.errorMessage=state.lastErrorMessage;
    completed.backups=state.runBackups;
    completed.warnings=state.runWarnings;
    completed.nextSteps=state.runNextSteps;
    completed.textLines=state.runTextLines;
    completed.hasReadTrace=state.hasReadTrace;
    completed.readTraceBytes=state.readTraceBytes;
    completed.readTraceCalls=state.readTraceCalls;
    completed.space=state.spaceTable;
    ui.completedRun=completed;
    // A run that was stopped, or that failed, has nothing to confirm and nothing to
    // show but its own ending, so it goes straight to the result dialogue. Success
    // means the command line reported a clean exit in its closing object: a run that
    // wrote no such object never reached a command at all, and the process's own
    // exit code is zero for every background process.
    const bool succeeded=!wasCancelled && !state.hasError && state.hasExitCode
        && state.lastExitCode==0;
    switch (finishedRun.purpose) {
        case RunPurpose::Scan:
        case RunPurpose::HistoryRefresh:
        case RunPurpose::ServiceCheck:
        case RunPurpose::SessionsList:
        case RunPurpose::DatabaseDiagnose:
        case RunPurpose::AgentsList:
            // These fill a tab or the drawer, so they need no dialogue of their own.
            if (finishedRun.purpose==RunPurpose::DatabaseDiagnose) {
                ui.selectedDatabasePath=finishedRun.invocation.databasePath;
                ui.selectedSessionIdentifier.clear();
                ui.currentTab=InterfaceTab::Databases;
            }
            break;
        case RunPurpose::CompactionPreview:
            if (succeeded) {
                RunRequest realRun;
                realRun.purpose=RunPurpose::CompactionRun;
                realRun.invocation=compactionInvocation(finishedRun.invocation.databasePath, false);
                addBackupOptions(realRun.invocation, state.settings);
                realRun.description="Compacting the event log";
                realRun.resultTitle="Compaction finished";
                realRun.followUp=FollowUp::Rescan;
                openConfirmation(
                    ui,
                    realRun,
                    "The dry run above changed nothing. The real run copies the database first, "
                    "removes the older copies of every message, and checks afterwards that every "
                    "conversation still holds its text. Type yes to run it for real.",
                    state.runTextLines);
            } else {
                ui.resultDialogOpen=true;
            }
            break;
        case RunPurpose::VacuumPreview:
            if (succeeded) {
                RunRequest realRun;
                realRun.purpose=RunPurpose::VacuumRun;
                realRun.invocation=vacuumInvocation(finishedRun.invocation.databasePath, false);
                addBackupOptions(realRun.invocation, state.settings);
                realRun.description="Vacuuming the database";
                realRun.resultTitle="Vacuum finished";
                realRun.followUp=FollowUp::Rescan;
                openConfirmation(
                    ui,
                    realRun,
                    "The dry run above changed nothing. The real run copies the database first, "
                    "rewrites the file so the unused pages go back to the drive, and checks the "
                    "result afterwards. Type yes to run it for real.",
                    state.runTextLines);
            } else {
                ui.resultDialogOpen=true;
            }
            break;
        case RunPurpose::ReclaimPreview:
            if (succeeded) {
                RunRequest realRun;
                realRun.purpose=RunPurpose::ReclaimRun;
                realRun.invocation=reclaimInvocation(
                    finishedRun.invocation.databasePath,
                    finishedRun.invocation.reclaimTargetBytes,
                    false);
                addBackupOptions(realRun.invocation, state.settings);
                realRun.description="Reclaiming space";
                realRun.resultTitle="Reclaim finished";
                realRun.followUp=FollowUp::Rescan;
                openConfirmation(
                    ui,
                    realRun,
                    "The dry run above changed nothing. The real run compacts the event log and "
                    "then vacuums the file, each with its own copy and its own check, until the "
                    "target is met. Type yes to run it for real.",
                    state.runTextLines);
            } else {
                ui.resultDialogOpen=true;
            }
            break;
        case RunPurpose::RecoverPlan:
            if (succeeded && state.recoveryPlan.isAvailable) {
                ui.planDialogOpen=true;
            } else {
                ui.resultDialogOpen=true;
            }
            break;
        case RunPurpose::BackupsList:
            ui.backupsDialogOpen=true;
            break;
        case RunPurpose::SpaceTable:
            if (state.spaceTable.isAvailable) {
                ui.spaceDialogOpen=true;
            } else {
                ui.resultDialogOpen=true;
            }
            break;
        default:
            // Everything that changed something, and everything that only wrote a
            // file, ends in the result dialogue.
            ui.resultDialogOpen=true;
            break;
    }
    // A run that changed something is followed by a fresh look, so the sizes on
    // screen are never the ones from before the change. A conversation removal
    // reloads the list first, so the removed conversation disappears from the
    // drawer, and that listing asks for the rescan in its own turn.
    if (finishedRun.followUp==FollowUp::Rescan) {
        if (finishedRun.purpose==RunPurpose::SessionDelete) {
            startRun(
                ui,
                state,
                runner,
                sessionsListRequest(finishedRun.invocation.databasePath, FollowUp::Rescan));
        } else {
            startRun(ui, state, runner, scanRequest());
        }
    } else if (finishedRun.followUp==FollowUp::CheckServiceStatus) {
        startRun(ui, state, runner, serviceStatusRequest());
    }
}
void renderOverviewTab(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    const bool isBusy=runner.isRunning();
    ImGui::TextWrapped("%s", scanSummarySentence(state, currentEpochMilliseconds()).c_str());
    ImGui::Spacing();
    const vector<const DatabaseRecord*> orderedRecords=largestDatabasesFirst(state);
    const DatabaseRecord* largestRecord=orderedRecords.empty() ? nullptr : orderedRecords.front();
    const long long targetBytes=state.settings.reclaimTargetGigabytes * BYTES_PER_GIGABYTE;
    // The card sizes itself to what it holds: the two large numbers and the button
    // below them. A fixed height cut the button off at a scaled display, where the
    // large text is twice as tall as the numbers it was measured against.
    if (ImGui::BeginChild(
            "OverviewTotals",
            ImVec2(0, 0),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        // The two numbers sit on one row and the button on the same row to their
        // right, which keeps the card short enough that the table below it stays
        // visible. The button takes its natural width: a fixed one clipped its last
        // letter the moment the display scaled the font up.
        ImGui::TextDisabled("Size of every database");
        ImGui::SameLine(0.0f, 56.0f);
        ImGui::TextDisabled("Unused space a vacuum would hand back");
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.6f);
        ImGui::TextUnformatted(formatBytesForWindow(totalDatabaseBytes(state)).c_str());
        ImGui::PopFont();
        ImGui::SameLine(0.0f, 56.0f);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.6f);
        ImGui::TextUnformatted(formatBytesForWindow(totalReclaimableBytes(state)).c_str());
        ImGui::PopFont();
        ImGui::SameLine(0.0f, 56.0f);
        CliInvocation reclaimBiggest;
        if (largestRecord!=nullptr) {
            reclaimBiggest=reclaimInvocation(largestRecord->filePath, targetBytes, true);
        }
        ImGui::BeginDisabled(isBusy || largestRecord==nullptr);
        if (ImGui::Button("Reclaim the biggest") && largestRecord!=nullptr) {
            RunRequest request;
            request.purpose=RunPurpose::ReclaimPreview;
            request.invocation=reclaimBiggest;
            request.description="Working out what a reclaim would do";
            request.resultTitle="Reclaim preview";
            startRun(ui, state, runner, request);
        }
        ImGui::EndDisabled();
        if (largestRecord!=nullptr) {
            showCommandTooltip(
                reclaimBiggest,
                string("The largest database is ") + largestRecord->filePath
                    + ". The reclaim aims to give back " + formatBytesForWindow(targetBytes)
                    + ", which is the target from the settings tab.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s",
            largestRecord==nullptr
                ? "There is nothing to reclaim yet."
                : (isBusy ? "A command is running." : "Changes nothing until you confirm it."));
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::TextUnformatted("The five largest databases");
    ImGui::Spacing();
    if (orderedRecords.empty()) {
        ImGui::TextDisabled(
            "%s",
            state.scan.isRunning
                ? "Still looking. The databases appear as they are measured."
                : "No databases were found on this machine.");
        return;
    }
    const size_t shownCount=orderedRecords.size()<5 ? orderedRecords.size() : 5;
    if (ImGui::BeginTable(
            "OverviewLargest",
            6,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Agent", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Agent"));
        ImGui::TableSetupColumn("Database", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Size"));
        ImGui::TableSetupColumn("Reclaimable", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Reclaimable"));
        ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Health"));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Reclaim"));
        ImGui::TableHeadersRow();
        for (size_t rowIndex=0; rowIndex<shownCount; ++rowIndex) {
            const DatabaseRecord& record=*orderedRecords[rowIndex];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(record.agentName.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(shortPath(record.filePath).c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", record.filePath.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytesForWindow(record.sizeBytes).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytesForWindow(record.reclaimableBytes).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(healthColour(record), "%s", healthWord(record).c_str());
            ImGui::TableNextColumn();
            const CliInvocation reclaimThis=reclaimInvocation(record.filePath, targetBytes, true);
            ImGui::BeginDisabled(isBusy || !acceptsDestructiveActions(record));
            ImGui::PushID(record.filePath.c_str());
            if (ImGui::SmallButton("Reclaim")) {
                RunRequest request;
                request.purpose=RunPurpose::ReclaimPreview;
                request.invocation=reclaimThis;
                request.description="Working out what a reclaim would do";
                request.resultTitle="Reclaim preview";
                startRun(ui, state, runner, request);
            }
            showCommandTooltip(
                reclaimThis,
                "What a reclaim of this database would do. The dry run changes nothing.");
            ImGui::PopID();
            ImGui::EndDisabled();
        }
        ImGui::EndTable();
    }
}
// The drawer under the database table: what one database holds, what the command
// line said about it, and every action that can be taken on it.
void renderDatabaseDrawer(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    const DatabaseRecord& record) {
    const bool isBusy=runner.isRunning();
    if (!ImGui::BeginChild("DatabaseDrawer", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }
    ImGui::TextColored(healthColour(record), "%s", healthWord(record).c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(record.agentName.c_str());
    ImGui::TextWrapped("%s", record.filePath.c_str());
    ImGui::Spacing();
    ImGui::SeparatorText("Measured by the command line");
    if (ImGui::BeginTable("DrawerMetrics", 2, ImGuiTableFlags_SizingFixedFit)) {
        // The label column is sized for the longest label actually printed, so the
        // "Older copies in the event log" row cannot clip at a scaled display.
        ImGui::TableSetupColumn(
            "",
            ImGuiTableColumnFlags_WidthFixed,
            columnWidthForText("Older copies in the event log"));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        metricRow("Size", formatBytesForWindow(record.sizeBytes));
        if (record.allocatedBytes>0) {
            metricRow("Space on the drive", formatBytesForWindow(record.allocatedBytes));
        }
        if (record.pageCount>0) {
            metricRow(
                "Pages",
                formatCountForWindow(record.pageCount) + " of "
                    + formatCountForWindow(record.pageSizeBytes) + " bytes");
            metricRow(
                "Unused pages inside",
                formatCountForWindow(record.freePageCount) + " ("
                    + formatBytesForWindow(record.reclaimableBytes) + ")");
        }
        if (record.hasWriteAheadLog) {
            metricRow(
                "Write ahead log",
                formatBytesForWindow(record.writeAheadLogBytes)
                    + " (the newest changes, not yet merged into the file)");
        }
        if (!record.journalMode.empty()) {
            metricRow("Journal", record.journalMode);
        }
        if (!record.schemaDescription.empty()) {
            metricRow("Schema", record.schemaDescription);
        }
        if (record.modifiedMilliseconds>0) {
            metricRow("Last written", formatTimestampForWindow(record.modifiedMilliseconds));
        }
        if (record.redundantEventRows>0) {
            metricRow(
                "Older copies in the event log",
                formatCountForWindow(record.redundantEventRows));
        }
        metricRow(
            "Integrity",
            record.integrity.empty() ? string("not checked") : record.integrity);
        ImGui::EndTable();
    }
    if (!record.integrityMessage.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", record.integrityMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    for (const string& warning : record.warnings) {
        ImGui::PushStyleColor(ImGuiCol_Text, attentionColour());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("Warning: %s", warning.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (!record.nextSteps.empty() && ImGui::CollapsingHeader("What the command suggests next")) {
        for (const string& nextStep : record.nextSteps) {
            ImGui::BulletText("%s", nextStep.c_str());
        }
    }
    ImGui::SeparatorText("Actions");
    const string refusal=refusalText(record);
    if (!refusal.empty()) {
        ImGui::TextDisabled("%s", refusal.c_str());
    }
    const long long targetBytes=state.settings.reclaimTargetGigabytes * BYTES_PER_GIGABYTE;
    // Reclaiming, compacting, and vacuuming all need a database the command line
    // could open, so the three of them share one disabled state.
    ImGui::BeginDisabled(!acceptsDestructiveActions(record) || isBusy);
    {
        const CliInvocation invocation=reclaimInvocation(record.filePath, targetBytes, true);
        if (ImGui::Button("Reclaim")) {
            RunRequest request;
            request.purpose=RunPurpose::ReclaimPreview;
            request.invocation=invocation;
            request.description="Working out what a reclaim would do";
            request.resultTitle="Reclaim preview";
            startRun(ui, state, runner, request);
        }
        showCommandTooltip(
            invocation,
            string("Compacts and then vacuums until ") + formatBytesForWindow(targetBytes)
                + " is given back, which is the target from the settings tab.");
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=compactionInvocation(record.filePath, true);
        ImGui::BeginDisabled(!record.compactionSupported);
        if (ImGui::Button("Preview compaction")) {
            RunRequest request;
            request.purpose=RunPurpose::CompactionPreview;
            request.invocation=invocation;
            request.description="Working out what a compaction would do";
            request.resultTitle="Compaction preview";
            startRun(ui, state, runner, request);
        }
        if (record.compactionSupported) {
            showCommandTooltip(
                invocation,
                "Removes the older copies of every message. The preview changes nothing.");
        } else {
            showExplanationTooltip(
                "This agent's event log layout is not documented in a way the tool trusts, so "
                "removing rows from it would be a guess. The vacuum is always safe.");
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=vacuumInvocation(record.filePath, true);
        if (ImGui::Button("Preview vacuum")) {
            RunRequest request;
            request.purpose=RunPurpose::VacuumPreview;
            request.invocation=invocation;
            request.description="Working out what a vacuum would do";
            request.resultTitle="Vacuum preview";
            startRun(ui, state, runner, request);
        }
        showCommandTooltip(
            invocation,
            "Hands the unused space back by rewriting the file. The preview changes nothing.");
    }
    ImGui::EndDisabled();
    // The actions below either work on a database that will not open or do not touch
    // the database at all, so they are only held back while a command is running.
    ImGui::BeginDisabled(isBusy);
    {
        const CliInvocation invocation=recoverPlanInvocation(record.filePath);
        if (ImGui::Button("Recovery plan")) {
            RunRequest request;
            request.purpose=RunPurpose::RecoverPlan;
            request.invocation=invocation;
            request.description="Reading the recovery plan";
            request.resultTitle="Recovery plan";
            startRun(ui, state, runner, request);
        }
        showCommandTooltip(
            invocation,
            "Reads what can be recovered from this database. Nothing is changed.");
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=backupsListInvocation(record.filePath);
        if (ImGui::Button("Backups...")) {
            RunRequest request;
            request.purpose=RunPurpose::BackupsList;
            request.invocation=invocation;
            request.description="Listing the safety copies";
            request.resultTitle="Safety copies";
            startRun(ui, state, runner, request);
        }
        showCommandTooltip(
            invocation,
            "Lists the safety copies beside this database, with their sizes and dates.");
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=sessionsListInvocation(record.filePath);
        if (ImGui::Button("Sessions...")) {
            ui.sessionsSectionOpen=true;
            startRun(ui, state, runner, sessionsListRequest(record.filePath, FollowUp::None));
        }
        showCommandTooltip(
            invocation,
            "Lists the largest conversations by the payload they store. This reads the "
            "conversation rows themselves, so it is done when you ask for it.");
    }
    const bool sessionIsChosen=!ui.selectedSessionIdentifier.empty()
        && state.sessionList.databasePath==record.filePath;
    const CliInvocation exportChosen=exportInvocation(
        record.filePath,
        ui.selectedSessionIdentifier,
        ui.exportFormat,
        string());
    ImGui::BeginDisabled(!sessionIsChosen);
    if (ImGui::Button("Export session...")) {
        ui.pendingFileDialog.kind=FileDialogRequest::Kind::SaveExport;
        ui.pendingFileDialog.databasePath=record.filePath;
        ui.pendingFileDialog.sessionIdentifier=ui.selectedSessionIdentifier;
        ui.pendingFileDialog.exportFormat=ui.exportFormat;
    }
    if (sessionIsChosen) {
        showCommandTooltip(
            exportChosen,
            "Writes the chosen conversation out as a document, in the format chosen below.");
    } else {
        showExplanationTooltip(
            "Load the conversations below and choose one first. This writes that conversation "
            "out as Markdown, JSON, or HTML.");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    {
        const CliInvocation invocation=spaceInvocation(record.filePath);
        if (ImGui::Button("Where the space is")) {
            RunRequest request;
            request.purpose=RunPurpose::SpaceTable;
            request.invocation=invocation;
            request.description="Dividing the file between its tables";
            request.resultTitle="Where the bytes are";
            startRun(ui, state, runner, request);
        }
        showCommandTooltip(
            invocation,
            "Adds up what each table and index holds, so the biggest one is named.");
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=diagnoseInvocation(record.filePath, false);
        if (ImGui::Button("Diagnose this database")) {
            startRun(ui, state, runner, databaseDiagnoseRequest(record.filePath, false));
        }
        showCommandTooltip(invocation, "Looks at this database again and updates this row.");
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=diagnoseInvocation(record.filePath, true);
        if (ImGui::Button("Full integrity check")) {
            startRun(ui, state, runner, databaseDiagnoseRequest(record.filePath, true));
        }
        showCommandTooltip(
            invocation,
            "Checks every page of the file. Slower, because it reads the whole database.");
    }
    ImGui::EndDisabled();
    // The conversation section. It is a section of the drawer rather than a dialogue,
    // because choosing a conversation means reading the list beside the buttons.
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Conversations", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderSessionSection(ui, state, runner, record);
    }
    ImGui::EndChild();
}
void renderSessionSection(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    const DatabaseRecord& record) {
    if (runner.isRunning() && ui.currentRun.purpose==RunPurpose::SessionsList) {
        ImGui::TextDisabled("Reading the list. The rows appear when the command has finished.");
        return;
    }
    if (state.sessionList.databasePath!=record.filePath) {
        ImGui::TextDisabled(
            "The conversations are read when you ask for them, because measuring them reads "
            "their own rows.");
        ImGui::BeginDisabled(runner.isRunning());
        if (ImGui::Button("Load the conversations")) {
            ui.sessionsSectionOpen=true;
            startRun(ui, state, runner, sessionsListRequest(record.filePath, FollowUp::None));
        }
        ImGui::EndDisabled();
        return;
    }
    if (state.sessionList.entries.empty()) {
        ImGui::TextDisabled("No conversation with stored parts was found in this database.");
        return;
    }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Format", ui.exportFormat.c_str())) {
        if (ImGui::Selectable("markdown", ui.exportFormat=="markdown")) {
            ui.exportFormat="markdown";
        }
        if (ImGui::Selectable("json", ui.exportFormat=="json")) {
            ui.exportFormat="json";
        }
        if (ImGui::Selectable("html", ui.exportFormat=="html")) {
            ui.exportFormat="html";
        }
        ImGui::EndCombo();
    }
    showExplanationTooltip(
        "What the export writes. The save dialogue offers the same three formats.");
    if (ImGui::BeginTable(
            "SessionTable",
            4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0, SESSION_TABLE_HEIGHT))) {
        ImGui::TableSetupColumn("Conversation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Payload", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Payload"));
        ImGui::TableSetupColumn("Parts", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Parts"));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Export") + columnWidthForText("Delete"));
        ImGui::TableHeadersRow();
        for (const SessionEntry& entry : state.sessionList.entries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(entry.identifier.c_str());
            const bool isSelected=entry.identifier==ui.selectedSessionIdentifier;
            if (ImGui::Selectable(
                    "##session",
                    isSelected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                ui.selectedSessionIdentifier=entry.identifier;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.identifier.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytesForWindow(entry.payloadBytes).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatCountForWindow(entry.partCount).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(runner.isRunning());
            if (ImGui::SmallButton("Export")) {
                ui.selectedSessionIdentifier=entry.identifier;
                ui.pendingFileDialog.kind=FileDialogRequest::Kind::SaveExport;
                ui.pendingFileDialog.databasePath=record.filePath;
                ui.pendingFileDialog.sessionIdentifier=entry.identifier;
                ui.pendingFileDialog.exportFormat=ui.exportFormat;
            }
            showCommandTooltip(
                exportInvocation(record.filePath, entry.identifier, ui.exportFormat, string()),
                "Writes this conversation out as a document, in the format chosen above.");
            ImGui::SameLine();
            CliInvocation deleteInvocation=sessionDeleteInvocation(record.filePath, entry.identifier);
            addBackupOptions(deleteInvocation, state.settings);
            if (ImGui::SmallButton("Delete")) {
                RunRequest deleteRequest;
                deleteRequest.purpose=RunPurpose::SessionDelete;
                deleteRequest.invocation=deleteInvocation;
                deleteRequest.description="Removing one conversation";
                deleteRequest.resultTitle="The conversation was removed";
                deleteRequest.followUp=FollowUp::Rescan;
                openConfirmation(
                    ui,
                    deleteRequest,
                    string("This removes the conversation ") + entry.identifier + " ("
                        + formatBytesForWindow(entry.payloadBytes)
                        + " of payload). The database is copied first and every other "
                          "conversation is checked afterwards. Type yes to remove it.",
                    vector<string>());
            }
            showCommandTooltip(
                deleteInvocation,
                "Copies the database, removes the conversation, checks that every other one still "
                "holds its text, and puts the copy back if a check does not pass.");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
void renderDatabasesTab(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::InputTextWithHint(
            "##filter",
            "Filter by agent or by path",
            ui.filterInput,
            sizeof(ui.filterInput))) {
        ui.filterText=ui.filterInput;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(ui.filterText.empty());
    if (ImGui::Button("Clear")) {
        ui.filterInput[0]='\0';
        ui.filterText.clear();
    }
    ImGui::EndDisabled();
    // The rows are ordered here rather than by the table, because a column of
    // formatted sizes cannot be ordered by the table at all: "1.2 GB" and "900 MB"
    // would be compared as text, which is the wrong answer.
    vector<const DatabaseRecord*> visibleRecords;
    for (const DatabaseRecord& record : state.databases) {
        if (matchesFilter(record, ui.filterText)) {
            visibleRecords.push_back(&record);
        }
    }
    const int sortColumn=ui.databaseSortColumn;
    const bool sortDescending=ui.databaseSortDescending;
    sort(
        visibleRecords.begin(),
        visibleRecords.end(),
        [sortColumn, sortDescending](const DatabaseRecord* left, const DatabaseRecord* right) {
            int comparison=0;
            if (sortColumn==0) {
                comparison=left->agentName.compare(right->agentName);
            } else if (sortColumn==2) {
                comparison=left->sizeBytes<right->sizeBytes
                    ? -1
                    : (left->sizeBytes>right->sizeBytes ? 1 : 0);
            } else if (sortColumn==3) {
                comparison=left->reclaimableBytes<right->reclaimableBytes
                    ? -1
                    : (left->reclaimableBytes>right->reclaimableBytes ? 1 : 0);
            } else if (sortColumn==4) {
                comparison=healthWord(*left).compare(healthWord(*right));
            }
            if (comparison==0) {
                comparison=left->filePath.compare(right->filePath);
            }
            return sortDescending ? comparison>0 : comparison<0;
        });
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%d of %d %s",
        static_cast<int>(visibleRecords.size()),
        static_cast<int>(state.databases.size()),
        state.databases.size()==1 ? "database" : "databases");
    // The table takes the smaller of its preferred height and a share of what is
    // left, so a machine with three databases does not leave the table half empty
    // and a machine with three hundred does not push the drawer off the screen.
    const float remainingHeight=ImGui::GetContentRegionAvail().y;
    const float preferredTableHeight=remainingHeight * 0.45f;
    const float tableHeight=preferredTableHeight<DATABASE_TABLE_HEIGHT
        ? preferredTableHeight
        : DATABASE_TABLE_HEIGHT;
    if (ImGui::BeginTable(
            "DatabaseTable",
            5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable
                | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0, tableHeight))) {
        ImGui::TableSetupColumn("Agent", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Agent"));
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Size",
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort
                | ImGuiTableColumnFlags_PreferSortDescending,
            columnWidthForText("Size"));
        ImGui::TableSetupColumn("Reclaimable", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Reclaimable"));
        ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Health"));
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* sortSpecs=ImGui::TableGetSortSpecs()) {
            if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount>0) {
                ui.databaseSortColumn=sortSpecs->Specs[0].ColumnIndex;
                ui.databaseSortDescending=
                    sortSpecs->Specs[0].SortDirection==ImGuiSortDirection_Descending;
                sortSpecs->SpecsDirty=false;
            }
        }
        for (const DatabaseRecord* record : visibleRecords) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(record->filePath.c_str());
            const bool isSelected=record->filePath==ui.selectedDatabasePath;
            // One selectable spans the whole row, so a click anywhere on it chooses
            // the database rather than only a click on its name.
            if (ImGui::Selectable(
                    "##row",
                    isSelected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0, 0))) {
                ui.selectedDatabasePath=record->filePath;
                ui.selectedSessionIdentifier.clear();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(record->agentName.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(shortPath(record->filePath).c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", record->filePath.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytesForWindow(record->sizeBytes).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytesForWindow(record->reclaimableBytes).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(healthColour(*record), "%s", healthWord(*record).c_str());
            ImGui::PopID();
        }
        if (visibleRecords.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(
                "%s",
                state.scan.isRunning
                    ? "Still looking. The databases appear as they are measured."
                    : "Nothing matches the filter.");
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    const DatabaseRecord* selectedRecord=findDatabaseRecord(state, ui.selectedDatabasePath);
    if (selectedRecord!=nullptr) {
        renderDatabaseDrawer(ui, state, runner, *selectedRecord);
    } else {
        ImGui::TextDisabled(
            "Choose a database to see what it holds and what can be done with it.");
    }
}
void renderHistoryTab(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    if (ui.historyRefreshRequested) {
        ui.historyRefreshRequested=false;
        if (!runner.isRunning()) {
            startRun(ui, state, runner, historyRefreshRequest());
        }
    }
    ImGui::BeginDisabled(runner.isRunning());
    if (ImGui::Button("Refresh")) {
        startRun(ui, state, runner, historyRefreshRequest());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(
        "The background service records one size per database per hour, which is what this table "
        "is drawn from.");
    ImGui::Spacing();
    if (state.historyRows.empty()) {
        ImGui::TextWrapped(
            "Nothing has been recorded yet. The service writes the first size the first time it "
            "runs, which is an hour after it is installed on the Service tab.");
        return;
    }
    long long rowsWithHistory=0;
    for (const HistoryRow& row : state.historyRows) {
        if (row.recordedPointCount>0) {
            rowsWithHistory++;
        }
    }
    if (rowsWithHistory==0) {
        ImGui::TextWrapped(
            "No sizes have been recorded for any database yet. This table fills in after the "
            "service has run for a few hours.");
    }
    if (ImGui::BeginTable(
            "HistoryTable",
            5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Database", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Last size", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Last size"));
        ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Change"));
        ImGui::TableSetupColumn("Per day", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Per day"));
        ImGui::TableSetupColumn("Drive fills in", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Drive fills in"));
        ImGui::TableHeadersRow();
        for (const HistoryRow& row : state.historyRows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(shortPath(row.databasePath).c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", row.databasePath.c_str());
            }
            ImGui::TableNextColumn();
            if (row.recordedPointCount==0) {
                ImGui::TextDisabled("not recorded");
            } else {
                ImGui::TextUnformatted(formatBytesForWindow(row.lastBytes).c_str());
            }
            ImGui::TableNextColumn();
            if (row.recordedPointCount==0) {
                ImGui::TextDisabled("-");
            } else if (row.changeBytes==0) {
                ImGui::TextUnformatted("unchanged");
            } else {
                const string direction=row.changeBytes>0 ? string("grew ") : string("shrank ");
                const long long magnitude=row.changeBytes>0 ? row.changeBytes : -row.changeBytes;
                ImGui::TextUnformatted((direction + formatBytesForWindow(magnitude)).c_str());
            }
            ImGui::TableNextColumn();
            if (row.recordedPointCount==0 || row.bytesPerDay==0) {
                ImGui::TextDisabled("-");
            } else if (row.bytesPerDay>0) {
                ImGui::TextUnformatted(
                    (string("+") + formatBytesForWindow(row.bytesPerDay)).c_str());
            } else {
                ImGui::TextUnformatted(
                    (string("-") + formatBytesForWindow(-row.bytesPerDay)).c_str());
            }
            ImGui::TableNextColumn();
            if (row.daysUntilDriveFull<0) {
                ImGui::TextDisabled(
                    "%s",
                    row.bytesPerDay<=0 ? "not growing" : "the free space could not be read");
            } else {
                ImGui::TextUnformatted(
                    (formatCountForWindow(row.daysUntilDriveFull) + " days").c_str());
            }
        }
        ImGui::EndTable();
    }
}
void renderServiceTab(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    const bool isBusy=runner.isRunning();
    if (ImGui::BeginChild(
            "ServiceStatus",
            ImVec2(0, 0),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        ImGui::TextUnformatted("The background watcher");
        ImGui::Separator();
        ImGui::TextWrapped("%s", serviceStateText(state.service).c_str());
        if (state.service.isAvailable) {
            ImGui::Spacing();
            ImGui::Text("Installed: %s", state.service.isInstalled ? "yes" : "no");
            ImGui::Text("Running:   %s", state.service.isRunning ? "yes" : "no");
            if (!state.service.locations.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("%s", state.service.locations.c_str());
                ImGui::PopTextWrapPos();
            }
            if (!state.service.lastLogLine.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("Most recent line in its log:");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("%s", state.service.lastLogLine.c_str());
                ImGui::PopTextWrapPos();
            }
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::BeginDisabled(isBusy);
    {
        const CliInvocation invocation=serviceActionInvocation("status");
        if (ImGui::Button("Check status")) {
            startRun(ui, state, runner, serviceStatusRequest());
        }
        showCommandTooltip(
            invocation,
            "Asks the operating system what the service is doing. Changes nothing.");
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=serviceActionInvocation("start");
        if (ImGui::Button("Start")) {
            RunRequest request;
            request.purpose=RunPurpose::ServiceAction;
            request.invocation=invocation;
            request.description="Starting the service";
            request.resultTitle="The service was started";
            request.followUp=FollowUp::CheckServiceStatus;
            startRun(ui, state, runner, request);
        }
        showCommandTooltip(
            invocation, "Starts the watcher if it is installed. Changes nothing about databases.");
    }
    ImGui::SameLine();
    {
        const CliInvocation invocation=serviceActionInvocation("stop");
        if (ImGui::Button("Stop")) {
            RunRequest request;
            request.purpose=RunPurpose::ServiceAction;
            request.invocation=invocation;
            request.description="Stopping the service";
            request.resultTitle="The service was stopped";
            request.followUp=FollowUp::CheckServiceStatus;
            startRun(ui, state, runner, request);
        }
        showCommandTooltip(invocation, "Stops the watcher. It stays installed.");
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::SeparatorText("Install");
    ImGui::TextWrapped(
        "Installing registers the watcher with the operating system so it starts when the "
        "computer does, and stops cleanly when it is shut down. It needs administrator rights: "
        "if the command fails with a permission message, start this window as an administrator "
        "and try again.");
    ImGui::BeginDisabled(isBusy);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputDouble("Warn past (GB)", &ui.serviceThresholdGigabytes, 1.0, 10.0, "%.1f");
    if (ui.serviceThresholdGigabytes<0.1) {
        ui.serviceThresholdGigabytes=0.1;
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputInt("Check every (seconds)", &ui.serviceIntervalSeconds, 60, 600);
    if (ui.serviceIntervalSeconds<60) {
        ui.serviceIntervalSeconds=60;
    }
    const long long thresholdBytes=static_cast<long long>(
        ui.serviceThresholdGigabytes * static_cast<double>(BYTES_PER_GIGABYTE));
    CliInvocation installInvocation=serviceActionInvocation("install");
    installInvocation.warningThresholdBytes=thresholdBytes;
    installInvocation.intervalSeconds=ui.serviceIntervalSeconds;
    installInvocation.answerYes=true;
    if (ImGui::Button("Install")) {
        RunRequest request;
        request.purpose=RunPurpose::ServiceAction;
        request.invocation=installInvocation;
        request.description="Installing the service";
        request.resultTitle="The service was installed";
        request.followUp=FollowUp::CheckServiceStatus;
        openConfirmation(
            ui,
            request,
            string("This registers the background watcher with the operating system, starts it, "
                "and warns once a database passes ") + formatBytesForWindow(thresholdBytes)
                + ", checking every " + formatCountForWindow(ui.serviceIntervalSeconds)
                + " seconds. It needs administrator rights. Type yes to go ahead.",
            vector<string>());
    }
    showCommandTooltip(
        installInvocation,
        "Installs and starts the background watcher with the threshold and the interval above.");
    ImGui::SameLine();
    CliInvocation uninstallInvocation=serviceActionInvocation("uninstall");
    uninstallInvocation.answerYes=true;
    if (ImGui::Button("Uninstall")) {
        RunRequest request;
        request.purpose=RunPurpose::ServiceAction;
        request.invocation=uninstallInvocation;
        request.description="Removing the service";
        request.resultTitle="The service was removed";
        request.followUp=FollowUp::CheckServiceStatus;
        openConfirmation(
            ui,
            request,
            "This removes the background watcher from the operating system. Your configuration "
            "file is kept, so installing again uses the same thresholds, and nothing is changed "
            "about your databases. Type yes to go ahead.",
            vector<string>());
    }
    showCommandTooltip(
        uninstallInvocation, "Removes the service. The databases themselves are untouched.");
    ImGui::EndDisabled();
}
void renderSettingsTab(InterfaceState& ui, GuiState& state) {
    InterfaceSettings& settings=state.settings;
    ImGui::TextUnformatted("What the actions use");
    ImGui::Separator();
    int targetGigabytes=static_cast<int>(settings.reclaimTargetGigabytes);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputInt("Reclaim target (GB)", &targetGigabytes)) {
        // A target of zero would make a reclaim pointless, so the field refuses to
        // go below one rather than accepting a number the command would reject.
        settings.reclaimTargetGigabytes=targetGigabytes<1 ? 1 : targetGigabytes;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        // The settings file is written once the user has finished with the field
        // rather than once per keystroke.
        ui.settingsAreDirty=true;
    }
    showExplanationTooltip(
        "How much space a reclaim aims to give back. The reclaim buttons pass it as "
        "--target <this>GB.");
    if (ImGui::Checkbox("Keep the safety copy after a successful run", &settings.keepSafetyCopy)) {
        ui.settingsAreDirty=true;
    }
    showExplanationTooltip(
        "Passes --keep-backup. By default the copy is removed once the checks pass, so the space "
        "it holds comes back too.");
    ImGui::SetNextItemWidth(420.0f);
    if (ImGui::InputText(
            "Backup destination",
            ui.backupDestinationInput,
            sizeof(ui.backupDestinationInput))) {
        settings.backupDestinationPath=ui.backupDestinationInput;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ui.settingsAreDirty=true;
    }
    ImGui::TextDisabled(
        "Empty means the safety copy is written beside the database. A named path is passed as "
        "--backup-to, and a named path also keeps the copy.");
    ImGui::Spacing();
    ImGui::SeparatorText("The command line program");
    const string locatedProgram=CliRunner::locateCliExecutable();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped(
        "%s",
        locatedProgram.empty()
            ? "Nothing was found beside this window, so the operating system is asked to find the "
              "program by name when an action runs."
            : locatedProgram.c_str());
    ImGui::TextDisabled("%s", CliRunner::locateCliExecutableDetails().c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::TextDisabled(
        "Every action in this window runs that program. A button's tooltip shows the exact "
        "command line it would run, and the same commands can be typed into a terminal.");
}
} // namespace
void renderTabs(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    if (!ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
        return;
    }
    // The tab bar owns its own selection, so a request to move to a tab is passed to
    // it as the one flag it understands. While that request is pending the bar is
    // still showing the tab it was on, and that tab reports itself as open one last
    // time; taking that report would cancel the request, so the report is ignored
    // until the bar has moved. A click on the bar is not a request: the tab that was
    // clicked reports itself as open, and that is what sets the state.
    const bool requestIsPending=ui.tabChangeRequested;
    const auto requestedTabFlags=[](const InterfaceState& state, InterfaceTab tab) {
        return state.tabChangeRequested && state.currentTab==tab
            ? ImGuiTabItemFlags_SetSelected
            : ImGuiTabItemFlags_None;
    };
    if (ImGui::BeginTabItem("Overview", nullptr, requestedTabFlags(ui, InterfaceTab::Overview))) {
        if (!requestIsPending) {
            selectTab(ui, InterfaceTab::Overview);
        }
        renderOverviewTab(ui, state, runner);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Databases", nullptr, requestedTabFlags(ui, InterfaceTab::Databases))) {
        if (!requestIsPending) {
            selectTab(ui, InterfaceTab::Databases);
        }
        renderDatabasesTab(ui, state, runner);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("History", nullptr, requestedTabFlags(ui, InterfaceTab::History))) {
        if (!requestIsPending) {
            selectTab(ui, InterfaceTab::History);
        }
        renderHistoryTab(ui, state, runner);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Service", nullptr, requestedTabFlags(ui, InterfaceTab::Service))) {
        if (!requestIsPending) {
            selectTab(ui, InterfaceTab::Service);
        }
        renderServiceTab(ui, state, runner);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings", nullptr, requestedTabFlags(ui, InterfaceTab::Settings))) {
        if (!requestIsPending) {
            selectTab(ui, InterfaceTab::Settings);
        }
        renderSettingsTab(ui, state);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    // A little empty space at the end of every tab, so a tab that scrolls ends with
    // its last line fully visible rather than flush against the clipping edge.
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing()));
    ui.tabChangeRequested=false;
}
float logPaneHeight() {
    // Three and a half lines of output, plus the child's own padding and border, so
    // the newest line is fully visible rather than half cut at the pane's edge, and
    // the tabs above keep enough room to show their tables.
    const ImGuiStyle& interfaceStyle=ImGui::GetStyle();
    return ImGui::GetTextLineHeightWithSpacing() * 3.5f
        + interfaceStyle.WindowPadding.y * 2.0f
        + interfaceStyle.ChildBorderSize * 2.0f;
}
float bottomBarHeight(bool logPaneIsOpen) {
    // The activity bar is one row, the log toggle is a second, and the separator
    // between the tabs and the bar takes a little more. Every part is measured from
    // the current font, so a scaled display cannot make the estimate too small and
    // push the log pane past the window's bottom edge, which is what clipped the
    // newest line when the numbers were fixed pixels.
    const ImGuiStyle& interfaceStyle=ImGui::GetStyle();
    float height=ImGui::GetFrameHeightWithSpacing() * 2.0f
        + interfaceStyle.ItemSpacing.y * 2.0f
        + ImGui::GetTextLineHeight() * 0.5f;
    if (logPaneIsOpen) {
        height+=logPaneHeight() + interfaceStyle.ItemSpacing.y;
    }
    return height;
}
void renderBottomBar(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    // The activity bar. It is always in the same place, so progress is never
    // somewhere a user has to go looking for it.
    if (runner.isRunning()) {
        ImGui::TextUnformatted(state.run.description.c_str());
        ImGui::SameLine();
        if (state.progress.totalUnits>0) {
            const float fraction=static_cast<float>(
                static_cast<double>(state.progress.completedUnits)
                / static_cast<double>(state.progress.totalUnits));
            char progressText[64]={0};
            snprintf(
                progressText,
                sizeof(progressText),
                "%lld of %lld",
                state.progress.completedUnits,
                state.progress.totalUnits);
            ImGui::ProgressBar(fraction, ImVec2(340.0f, 0.0f), progressText);
        } else {
            // No total yet: the bar slides rather than claiming a percentage, which
            // Dear ImGui draws for a negative fraction.
            ImGui::ProgressBar(
                -1.0f * static_cast<float>(ImGui::GetTime()),
                ImVec2(340.0f, 0.0f),
                "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            // Nothing is unwound here: the command line's own checks are what protect
            // the data, and they run inside the command that is being stopped.
            runner.cancel();
            ui.runWasCancelled=true;
        }
        showExplanationTooltip(
            "Stops the command. Anything it had already done is left to the tool's own checks, "
            "which put the original back when a check does not pass.");
    } else if (!state.lastRunEnding.empty()) {
        ImGui::TextDisabled("%s", state.lastRunEnding.c_str());
    } else {
        ImGui::TextDisabled("Nothing is running.");
    }
    if (ImGui::Button(ui.logPaneIsOpen ? "Hide the log" : "Show the log")) {
        ui.logPaneIsOpen=!ui.logPaneIsOpen;
        ui.settingsAreDirty=true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%d %s",
        static_cast<int>(state.logLines.size()),
        state.logLines.size()==1 ? "line" : "lines");
    if (!ui.logPaneIsOpen) {
        return;
    }
    if (ImGui::BeginChild(
            "LogPane",
            ImVec2(0, logPaneHeight()),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar)) {
        // Whether the pane follows the newest line is decided from where it was
        // before this frame's lines were added. A reader who has scrolled up is
        // reading, and a line arriving must not yank the view away from them.
        if (ImGui::GetScrollMaxY()>0.0f) {
            ui.logFollowsNewestOutput=ImGui::GetScrollY()>=ImGui::GetScrollMaxY() - 1.0f;
        }
        if (!ui.logFollowsNewestOutput) {
            if (ImGui::SmallButton("Follow the newest line")) {
                ui.logFollowsNewestOutput=true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("You scrolled up, so the view stays where it is.");
        }
        for (const string& logLine : state.logLines) {
            ImGui::TextUnformatted(logLine.c_str());
        }
        if (ui.logFollowsNewestOutput) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}
namespace {
// The confirmation dialogue: the question, the lines the preview printed, and the
// word that has to be typed.
void renderConfirmationDialog(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    if (ui.confirmationOpen) {
        ImGui::OpenPopup("Confirm");
        ui.confirmationOpen=false;
    }
    if (!ImGui::BeginPopupModal("Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
    ImGui::TextUnformatted(ui.confirmationSentence.c_str());
    ImGui::PopTextWrapPos();
    if (!ui.confirmationPreviewLines.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("What the dry run printed:");
        textLineBlock("ConfirmationPreview", ui.confirmationPreviewLines, 140.0f);
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Type yes to go ahead, or close this box to stop.");
    const bool submittedWithEnter=ImGui::InputText(
        "Confirmation",
        ui.confirmationInput,
        sizeof(ui.confirmationInput),
        ImGuiInputTextFlags_EnterReturnsTrue);
    // The word is compared exactly, the way the command line compares it: "yes" is
    // the whole answer, and anything else, including "Yes" or a trailing space, is
    // not.
    const bool wordMatches=string(ui.confirmationInput)==CONFIRMATION_WORD;
    ImGui::BeginDisabled(!wordMatches || runner.isRunning());
    const bool pressedGoAhead=ImGui::Button("Go ahead");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool pressedStop=ImGui::Button("Stop");
    if ((submittedWithEnter || pressedGoAhead) && wordMatches && !runner.isRunning()) {
        startRun(ui, state, runner, ui.confirmedRun);
        ImGui::CloseCurrentPopup();
    } else if (pressedStop || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
// The result dialogue: the verdict, the numbers the command reported, whatever it
// complained about, and the lines it printed.
void renderResultDialog(InterfaceState& ui) {
    if (ui.resultDialogOpen) {
        ImGui::OpenPopup("Result");
        ui.resultDialogOpen=false;
    }
    // The dialogue is as large as its content needs, with a floor so that a run that
    // reported two numbers does not produce a dialogue too narrow to read a path in.
    ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopupModal("Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const CompletedRun& completedRun=ui.completedRun;
    ImGui::TextUnformatted(completedRun.title.c_str());
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 46.0f);
    ImGui::TextUnformatted(completedRun.verdict.c_str());
    ImGui::PopTextWrapPos();
    if (completedRun.hasError && !completedRun.errorMessage.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, problemColour());
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 46.0f);
        ImGui::TextWrapped("The command's own words: %s", completedRun.errorMessage.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (!completedRun.fields.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("What the command reported");
        if (ImGui::BeginTable(
                "ResultFields",
                2,
                ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg
                    | ImGuiTableFlags_Borders)) {
            for (const ResultField& field : completedRun.fields) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(field.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatResultFieldValue(field).c_str());
            }
            ImGui::EndTable();
        }
    }
    if (!completedRun.backups.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("The safety copy this run made");
        for (const BackupRecord& backup : completedRun.backups) {
            ImGui::BulletText(
                "%s  (%s)", backup.filePath.c_str(),
                formatBytesForWindow(backup.sizeBytes).c_str());
        }
    }
    if (completedRun.hasReadTrace) {
        ImGui::Spacing();
        ImGui::Text(
            "Reads: %s bytes in %s read calls, counted through the database library's own file "
            "layer.",
            formatCountForWindow(completedRun.readTraceBytes).c_str(),
            formatCountForWindow(completedRun.readTraceCalls).c_str());
    }
    if (!completedRun.warnings.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("What the command wanted you to know");
        ImGui::PushStyleColor(ImGuiCol_Text, attentionColour());
        for (const string& warning : completedRun.warnings) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 46.0f);
            ImGui::BulletText("%s", warning.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::PopStyleColor();
    }
    if (!completedRun.nextSteps.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("What the command suggests next");
        for (const string& nextStep : completedRun.nextSteps) {
            ImGui::BulletText("%s", nextStep.c_str());
        }
    }
    if (!completedRun.textLines.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Everything the command printed");
        textLineBlock("ResultLines", completedRun.textLines, DIALOG_BODY_HEIGHT);
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("The whole output is in the log pane behind this box.");
    ImGui::EndPopup();
}
// The recovery plan: the steps the command line recommends, and the one action that
// changes anything.
void renderPlanDialog(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    if (ui.planDialogOpen) {
        ImGui::OpenPopup("Recovery plan");
        ui.planDialogOpen=false;
    }
    if (!ImGui::BeginPopupModal("Recovery plan", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const RecoveryPlan& plan=state.recoveryPlan;
    if (!plan.agentName.empty() || !plan.filePath.empty()) {
        ImGui::TextWrapped("%s  %s", plan.agentName.c_str(), plan.filePath.c_str());
        ImGui::Separator();
    }
    if (ImGui::BeginChild("PlanSteps", ImVec2(680.0f, PLAN_BODY_HEIGHT), ImGuiChildFlags_Borders)) {
        int stepNumber=0;
        for (const PlanStep& step : plan.steps) {
            stepNumber++;
            ImGui::PushTextWrapPos(0.0f);
            if (step.changesData) {
                ImGui::PushStyleColor(ImGuiCol_Text, attentionColour());
                ImGui::TextWrapped(
                    "Step %d. %s  [%s]",
                    stepNumber,
                    step.title.c_str(),
                    step.severity.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextWrapped(
                    "Step %d. %s  [%s]",
                    stepNumber,
                    step.title.c_str(),
                    step.severity.c_str());
            }
            ImGui::TextWrapped("%s", step.explanation.c_str());
            ImGui::PopTextWrapPos();
            if (!step.command.empty()) {
                ImGui::TextDisabled("%s", step.command.c_str());
            }
            if (step.changesData) {
                ImGui::TextDisabled(
                    "This step writes to the file, so the tool copies it first and asks before "
                    "running it.");
            }
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextDisabled(
        "The plan above is what the tool found. Nothing has been changed, and nothing changes "
        "until a rebuild is confirmed.");
    ImGui::BeginDisabled(runner.isRunning());
    {
        const CliInvocation rebuildInvocation=recoverRebuildInvocation(plan.filePath);
        if (ImGui::Button("Rebuild a working copy")) {
            RunRequest rebuildRequest;
            rebuildRequest.purpose=RunPurpose::RecoverRebuild;
            rebuildRequest.invocation=rebuildInvocation;
            rebuildRequest.description="Rebuilding a working copy";
            rebuildRequest.resultTitle="Recovery rebuild finished";
            rebuildRequest.followUp=FollowUp::Rescan;
            ImGui::CloseCurrentPopup();
            openConfirmation(
                ui,
                rebuildRequest,
                "This builds a working copy holding every row that can still be read. The "
                "original is never modified. Type yes to go ahead.",
                vector<string>());
        }
        showCommandTooltip(
            rebuildInvocation,
            "Builds a copy holding every row that can still be read. The original is not "
            "modified.");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
// The safety copies of one database: what the listing said, and the three things
// that can be done with a copy.
void renderBackupsDialog(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    if (ui.backupsDialogOpen) {
        ImGui::OpenPopup("Safety copies");
        ui.backupsDialogOpen=false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(620.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopupModal("Safety copies", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const CompletedRun& completedRun=ui.completedRun;
    ImGui::TextWrapped(
        "A safety copy is written before anything that changes a database, and removed again once "
        "the checks pass unless the settings say to keep it.");
    if (!completedRun.backups.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Copies made during this run");
        if (ImGui::BeginTable("BackupRows", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Copy", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Size"));
            ImGui::TableHeadersRow();
            for (const BackupRecord& backup : completedRun.backups) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(backup.filePath.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatBytesForWindow(backup.sizeBytes).c_str());
            }
            ImGui::EndTable();
        }
    }
    if (!completedRun.textLines.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("What the listing printed");
        textLineBlock("BackupLines", completedRun.textLines, DIALOG_BODY_HEIGHT);
    }
    ImGui::Spacing();
    ImGui::Separator();
    const string databasePath=completedRun.databasePath;
    ImGui::BeginDisabled(runner.isRunning() || databasePath.empty());
    {
        CliInvocation verifyInvocation;
        verifyInvocation.command="backups";
        verifyInvocation.action="verify";
        verifyInvocation.databasePath=databasePath;
        verifyInvocation.backupsAll=true;
        if (ImGui::Button("Verify every copy")) {
            RunRequest request;
            request.purpose=RunPurpose::BackupsAction;
            request.invocation=verifyInvocation;
            request.description="Testing whether the copies can be put back";
            request.resultTitle="The restore drill finished";
            startRun(ui, state, runner, request);
            ImGui::CloseCurrentPopup();
        }
        showCommandTooltip(
            verifyInvocation,
            "Copies each copy to a scratch file, checks it there, and removes the scratch file "
            "again. That is the only way to know a copy really can be put back.");
    }
    ImGui::SameLine();
    {
        CliInvocation restoreInvocation;
        restoreInvocation.command="backups";
        restoreInvocation.action="restore";
        restoreInvocation.databasePath=databasePath;
        restoreInvocation.answerYes=true;
        if (ImGui::Button("Put the newest copy back")) {
            RunRequest request;
            request.purpose=RunPurpose::BackupsAction;
            request.invocation=restoreInvocation;
            request.description="Putting the newest safety copy back";
            request.resultTitle="The safety copy was put back";
            request.followUp=FollowUp::Rescan;
            openConfirmation(
                ui,
                request,
                "This puts the newest safety copy back over the database. The file that is there "
                "now is moved aside first, so nothing is lost either way. Type yes to go ahead.",
                vector<string>());
        }
        showCommandTooltip(
            restoreInvocation,
            "Puts the newest copy back over the database and checks the result afterwards.");
    }
    ImGui::SameLine();
    {
        CliInvocation deleteInvocation;
        deleteInvocation.command="backups";
        deleteInvocation.action="delete";
        deleteInvocation.databasePath=databasePath;
        deleteInvocation.olderThanDays=ui.backupsDeleteOlderThanDays;
        deleteInvocation.answerYes=true;
        if (ImGui::Button("Delete the older copies")) {
            RunRequest request;
            request.purpose=RunPurpose::BackupsAction;
            request.invocation=deleteInvocation;
            request.description="Removing safety copies";
            request.resultTitle="The safety copies were removed";
            openConfirmation(
                ui,
                request,
                string("This removes every safety copy older than ")
                    + formatCountForWindow(ui.backupsDeleteOlderThanDays)
                    + " days. A copy is the way back if something turns out to be wrong with the "
                      "database, so remove them only when you are satisfied with it. Type yes to "
                      "go ahead.",
                vector<string>());
        }
        showCommandTooltip(
            deleteInvocation,
            "Removes the copies older than the age beside this button. The database itself is "
            "untouched.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("days old", &ui.backupsDeleteOlderThanDays, 1, 7);
        if (ui.backupsDeleteOlderThanDays<0) {
            ui.backupsDeleteOlderThanDays=0;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
// Where one database's bytes are: the tables and indexes, largest first.
void renderSpaceDialog(InterfaceState& ui) {
    if (ui.spaceDialogOpen) {
        ImGui::OpenPopup("Where the bytes are");
        ui.spaceDialogOpen=false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(620.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopupModal(
            "Where the bytes are", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const SpaceTable& spaceTable=ui.completedRun.space;
    ImGui::TextWrapped("%s", spaceTable.databasePath.c_str());
    ImGui::Separator();
    ImGui::Text(
        "Size: %s in %s pages of %s bytes",
        formatBytesForWindow(spaceTable.sizeBytes).c_str(),
        formatCountForWindow(spaceTable.pageCount).c_str(),
        formatCountForWindow(spaceTable.pageSizeBytes).c_str());
    ImGui::Text(
        "Free pages: %s, which a vacuum hands back to the drive",
        formatBytesForWindow(spaceTable.freeBytes).c_str());
    ImGui::Spacing();
    if (ImGui::BeginTable(
            "SpaceRows",
            3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0, SPACE_TABLE_HEIGHT))) {
        ImGui::TableSetupColumn("Table or index", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Kind"));
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, columnWidthForText("Bytes"));
        ImGui::TableHeadersRow();
        for (const SpaceEntry& entry : spaceTable.entries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.isIndex ? "index" : "table");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytesForWindow(entry.bytes).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
} // namespace
void renderDialogs(InterfaceState& ui, GuiState& state, CliRunner& runner) {
    renderConfirmationDialog(ui, state, runner);
    renderResultDialog(ui);
    renderPlanDialog(ui, state, runner);
    renderBackupsDialog(ui, state, runner);
    renderSpaceDialog(ui);
}
void renderAboutDialog(
    bool& aboutOpen,
    const vector<AgentRecord>& agents,
    unsigned long long logoTextureHandle,
    const string& versionText,
    const string& platformText) {
    if (aboutOpen) {
        ImGui::OpenPopup("About agent-db-doctor");
        aboutOpen=false;
    }
    if (!ImGui::BeginPopupModal(
            "About agent-db-doctor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (logoTextureHandle!=0ULL) {
        ImGui::Image(static_cast<ImTextureID>(logoTextureHandle), ImVec2(128.0f, 128.0f));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("agent-db-doctor %s", versionText.c_str());
    ImGui::Text("Built for %s.", platformText.c_str());
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    ImGui::TextWrapped(
        "A doctor for the databases AI coding agents use to remember your work. This window is a "
        "view of the command line program beside it: it runs the same program with arguments, "
        "reads the objects it prints, and draws them.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Separator();
    if (agents.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
        ImGui::TextWrapped(
            "The list of supported agents comes from the agents command and is not loaded yet. "
            "Open this box again once a command has run and it will be here.");
        ImGui::PopTextWrapPos();
    } else {
        ImGui::Text("%d agents are supported by this build.", static_cast<int>(agents.size()));
        if (ImGui::BeginChild(
                "AboutAgents",
                ImVec2(560.0f, ABOUT_AGENT_LIST_HEIGHT),
                ImGuiChildFlags_Borders)) {
            for (const AgentRecord& agent : agents) {
                ImGui::TextUnformatted(agent.name.c_str());
                ImGui::SameLine();
                if (agent.foundPaths.empty()) {
                    ImGui::TextDisabled("nothing found on this machine");
                } else {
                    ImGui::TextDisabled(
                        "%d %s",
                        static_cast<int>(agent.foundPaths.size()),
                        agent.foundPaths.size()==1 ? "database found" : "databases found");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextWrapped("%s", agent.storage.c_str());
                    for (const string& foundPath : agent.foundPaths) {
                        ImGui::BulletText("%s", foundPath.c_str());
                    }
                    if (agent.foundPaths.empty()) {
                        ImGui::TextDisabled("Looked in:");
                        for (const string& candidatePath : agent.candidatePaths) {
                            ImGui::BulletText("%s", candidatePath.c_str());
                        }
                    }
                    if (agent.supportsCompaction) {
                        ImGui::TextDisabled("The compaction command understands its event log.");
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::Spacing();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
}
