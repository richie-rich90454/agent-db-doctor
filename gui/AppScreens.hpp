// The screens: every tab, the activity bar, the log pane, and every dialogue.
//
// A screen draws and it asks for commands; it never runs one itself, and it never
// opens a database. A press of a button becomes a RunRequest, which is turned into
// a child process by CliRunner and whose ending is handled here: a preview leads to
// the confirmation for the real run, a plan leads to the plan dialogue, a listing
// leads to the dialogue that shows it, and everything else leads to the result
// dialogue.
//
// One action at a time is the rule the whole file is built around. Every control
// that starts something is disabled while a command is in flight, and the state
// holds one run description rather than a list of them.
#ifndef AGENT_DB_DOCTOR_GUI_APPSCREENS_HPP
#define AGENT_DB_DOCTOR_GUI_APPSCREENS_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the runner that starts the commands.
#include "CliRunner.hpp"
// Provides the state the events fill.
#include "GuiState.hpp"
using namespace std;
namespace AgentDbDoctor::Gui {
// Which tab the window is showing.
enum class InterfaceTab {
    Overview,
    Databases,
    History,
    Service,
    Settings
};
// What a run was for. The purpose decides what happens when it ends.
enum class RunPurpose {
    // Nothing asked for.
    None,
    // A look at every database.
    Scan,
    // A look at one database, named by the user or by an action.
    DatabaseDiagnose,
    // A reading of the recorded growth.
    HistoryRefresh,
    // A question to the operating system about the background service.
    ServiceCheck,
    // A listing of one database's conversations.
    SessionsList,
    // A compaction that changes nothing, to learn the numbers.
    CompactionPreview,
    // A compaction that removes rows, after the typed confirmation.
    CompactionRun,
    // A vacuum that changes nothing, to learn the numbers.
    VacuumPreview,
    // A vacuum that rewrites the file, after the typed confirmation.
    VacuumRun,
    // A recovery plan that runs nothing.
    RecoverPlan,
    // A recovery rebuild, after the typed confirmation.
    RecoverRebuild,
    // A reclaim that changes nothing, to learn the numbers.
    ReclaimPreview,
    // A reclaim between the target and nothing, after the typed confirmation.
    ReclaimRun,
    // The removal of one conversation, after the typed confirmation.
    SessionDelete,
    // The writing out of one conversation.
    ExportSession,
    // A listing of the safety copies beside a database.
    BackupsList,
    // Verify, restore, or delete safety copies.
    BackupsAction,
    // Where one database's bytes are.
    SpaceTable,
    // Install, uninstall, start, or stop the service.
    ServiceAction,
    // A listing of the agents the tool supports, for the about dialogue.
    AgentsList
};
// What must happen after a run that changed something, so the window never shows
// the numbers from before the change.
enum class FollowUp {
    // Nothing.
    None,
    // A fresh look at every database.
    Rescan,
    // A question to the operating system about the service.
    CheckServiceStatus
};
// One command the window wants to run, with what is needed to act on its ending.
struct RunRequest final {
    // What the run is for.
    RunPurpose purpose=RunPurpose::None;
    // The command and its arguments.
    CliInvocation invocation;
    // What the activity bar says while it runs, for example "Vacuuming".
    string description;
    // The result dialogue's title, for example "Vacuum finished".
    string resultTitle;
    // What to do when it finishes.
    FollowUp followUp=FollowUp::None;
};
// A file dialog only the main loop can open, because it owns the window and pumps
// the events the dialog answers through. The screens leave a request here and the
// main loop fills the answer back in.
struct FileDialogRequest final {
    // Which dialog to open.
    enum class Kind {
        None,
        // Choose a database file to look at.
        OpenDatabase,
        // Choose where a conversation is written out.
        SaveExport
    };
    Kind kind=Kind::None;
    // For a save dialog: which database the conversation is in, which conversation
    // it is, and in which format it is written.
    string databasePath;
    string sessionIdentifier;
    string exportFormat;
};
// One finished run, copied out of the live state so that a dialogue keeps its own
// numbers while the command its ending started (a rescan, usually) fills the state
// in again.
struct CompletedRun final {
    // True when there is a run to show.
    bool isAvailable=false;
    // What the run was for.
    RunPurpose purpose=RunPurpose::None;
    // The database the run worked on, which the safety copy dialogue needs because
    // the live run may already have moved on.
    string databasePath;
    // The dialogue's title and its one line verdict.
    string title;
    string verdict;
    // How it ended.
    int exitCode=0;
    bool wasCancelled=false;
    // What it reported.
    vector<ResultField> fields;
    bool hasError=false;
    string errorMessage;
    // The safety copies it made, the warnings it raised, and what it suggests next.
    vector<BackupRecord> backups;
    vector<string> warnings;
    vector<string> nextSteps;
    // The lines it printed, which every dialogue can offer under its own content.
    vector<string> textLines;
    // What it read, for a run that counted its reads.
    bool hasReadTrace=false;
    long long readTraceBytes=0;
    long long readTraceCalls=0;
    // Where the bytes are, for the space dialogue.
    SpaceTable space;
};
// Everything the interface remembers between frames that is not part of the state
// the events fill: which tab shows, what is selected, what is open, and what the
// user has typed into the boxes that are not settings.
struct InterfaceState final {
    // Which tab is showing.
    InterfaceTab currentTab=InterfaceTab::Overview;
    // True when the tab bar has to be moved to `currentTab` on the next frame. The
    // tab bar owns its own selection, because that is what makes a click on it work;
    // a menu item, or the smoke test, asks for a tab by setting this flag, and the
    // tab bar is told once.
    bool tabChangeRequested=false;
    // The databases tab. The filter is a member array rather than a string rebuilt
    // every frame, because Dear ImGui keeps the editing state of the field inside
    // the field's own storage.
    char filterInput[96]={0};
    string filterText;
    string selectedDatabasePath;
    // Which column the database table is sorted by, and which way. The default is
    // the size, largest first, because that is the question the tab answers.
    int databaseSortColumn=2;
    bool databaseSortDescending=true;
    // The conversation section of the drawer: whether it is open, which row is
    // chosen, and which format an export writes.
    bool sessionsSectionOpen=false;
    string selectedSessionIdentifier;
    string exportFormat="markdown";
    // The service tab's inputs: the size a database must reach before the service
    // mentions it, and how often it checks.
    double serviceThresholdGigabytes=50.0;
    int serviceIntervalSeconds=3600;
    // Set when the history tab is opened, so its first visit reads the history.
    bool historyRefreshRequested=false;
    // The run in flight, and what the window must do with its ending.
    RunRequest currentRun;
    bool runWasCancelled=false;
    // The dialogs.
    bool confirmationOpen=false;
    string confirmationSentence;
    // The typed word, in a member array so the field keeps its editing state, and
    // compared after every keystroke.
    char confirmationInput[24]={0};
    // The lines of the preview the confirmation is asking about, shown under the
    // question so the user confirms what they can see.
    vector<string> confirmationPreviewLines;
    // The run the confirmation would start.
    RunRequest confirmedRun;
    bool resultDialogOpen=false;
    bool planDialogOpen=false;
    bool backupsDialogOpen=false;
    bool spaceDialogOpen=false;
    bool aboutOpen=false;
    // The finished run every dialogue reads.
    CompletedRun completedRun;
    // The file dialog the main loop must open, and the path it answered with.
    FileDialogRequest pendingFileDialog;
    // How old a safety copy must be before the delete button removes it.
    int backupsDeleteOlderThanDays=7;
    // The log pane: whether it is open, and whether it follows the newest line. It
    // starts closed, because the tab content is what a person came for and the log
    // is the child process's raw output; the activity bar still shows how the last
    // command ended, and one press opens the pane.
    bool logPaneIsOpen=false;
    bool logFollowsNewestOutput=true;
    // The settings text box. Like the filter, it is a member array because Dear
    // ImGui edits the storage in place; the settings are written from it on change.
    char backupDestinationInput[512]={0};
    // True when a setting changed and the settings file has to be written again.
    bool settingsAreDirty=false;
};
// The requests a menu or a row builds. They are separate functions rather than
// inline construction because every one of them is built from two places (a button
// and the ending of the run before it), and two copies of an argument list is how
// a flag goes missing from one of them.
RunRequest scanRequest();
RunRequest agentsRequest();
RunRequest databaseDiagnoseRequest(const string& databasePath, bool integrityCheck);
RunRequest sessionsListRequest(const string& databasePath, FollowUp followUp);
RunRequest historyRefreshRequest();
RunRequest serviceStatusRequest();
// Starts a command. Does nothing while one is already running, which is the rule
// every button obeys; the buttons disable themselves for the same reason, so this
// is the backstop rather than the answer the user sees.
void startRun(InterfaceState& ui, GuiState& state, CliRunner& runner, const RunRequest& request);
// Takes the lines the child produced, applies them to the state, and closes a run
// whose child has ended. Call once per frame before drawing.
void pollRunner(InterfaceState& ui, GuiState& state, CliRunner& runner);
// Opens the confirmation dialogue: the sentence, the lines of the preview it is
// about, and the run it would start.
void openConfirmation(
    InterfaceState& ui,
    const RunRequest& runRequest,
    const string& sentence,
    const vector<string>& previewLines);
// Switches tabs, asking for what the new tab needs.
void selectTab(InterfaceState& ui, InterfaceTab tab);
// Draws the tab bar and the selected tab.
void renderTabs(InterfaceState& ui, GuiState& state, CliRunner& runner);
// Draws the activity bar and the log pane.
void renderBottomBar(InterfaceState& ui, GuiState& state, CliRunner& runner);
// The height the log pane needs, computed from the current font so a scaled display
// never clips its newest line. The main loop reserves the same height, which is what
// keeps the pane inside the window.
float logPaneHeight();
// The height the whole bottom area needs: the activity bar, the log toggle row, and
// the log pane when it is open. The main loop subtracts it from the space the tabs
// may use, so the pane can never be cut off by the window's bottom edge.
float bottomBarHeight(bool logPaneIsOpen);
// Draws the confirmation, result, plan, safety copy, space, and about dialogues.
// Call once per frame, after the tabs.
void renderDialogs(InterfaceState& ui, GuiState& state, CliRunner& runner);
// Draws the about dialogue. `logoTextureHandle` is the logo texture as the value
// Dear ImGui wants, or zero when the artwork beside the binary is missing. When the
// agent list is empty the dialogue says how to ask for it.
void renderAboutDialog(
    bool& aboutOpen,
    const vector<AgentRecord>& agents,
    unsigned long long logoTextureHandle,
    const string& versionText,
    const string& platformText);
}
#endif
