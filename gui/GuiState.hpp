// The state of the window, and the pure functions that turn the command line's
// machine readable output into it.
//
// Everything the window shows comes from one of two places: the JSON events the
// child process writes, which this file turns into records, and the settings the
// user chose. There is no third source. In particular there is no copy of the
// tool's diagnosis logic here: a number the window draws is a number the command
// line printed, which is what keeps the two front ends from ever disagreeing.
//
// Nothing in this file includes SDL or Dear ImGui, and nothing in it includes a
// header from the core library. That is deliberate: the state and the event
// parsing are the part of the graphical interface that can be tested without a
// window, and the unit tests do exactly that.
#ifndef AGENT_DB_DOCTOR_GUI_GUISTATE_HPP
#define AGENT_DB_DOCTOR_GUI_GUISTATE_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the JsonValue the events arrive as.
#include "Json.hpp"
using namespace std;
namespace AgentDbDoctor::Gui {
// How many log lines the window keeps. A diagnosis of twenty databases prints a
// few hundred lines, and a reader scrolling back wants the recent ones; keeping
// every line of a long session would grow without bound.
inline constexpr size_t LOG_LINE_LIMIT=500;
// The theme choices, as the theme menu numbers them.
inline constexpr int THEME_FOLLOW_SYSTEM=0;
inline constexpr int THEME_DARK=1;
inline constexpr int THEME_LIGHT=2;
// One binary gigabyte. It is the unit the reclaim target is chosen in and the
// unit the service thresholds are shown in, because a number of bytes is not a
// number anybody chooses.
inline constexpr long long BYTES_PER_GIGABYTE=1024LL * 1024LL * 1024LL;
// One table's row count from a `database` event. The count can be approximate,
// and the event says when it is, so the window can say so too.
struct RowCount final {
    // The table's name as the database spells it.
    string tableName;
    // The rows it holds, or the estimate of them.
    long long rowCount=0;
    // True when the row count was estimated rather than counted.
    bool isApproximate=false;
};
// One database, as `diagnose --machine-readable` described it.
struct DatabaseRecord final {
    // The file (or folder) the agent keeps its data in.
    string filePath;
    // The agent the data belongs to.
    string agentName;
    // True when something is at that path.
    bool exists=false;
    // True when the path is a folder of text files rather than a database.
    bool isDirectory=false;
    // True when the command line called the database healthy.
    bool isHealthy=false;
    // The size the file reports.
    long long sizeBytes=0;
    // The space the file actually occupies on the drive.
    long long allocatedBytes=0;
    // True when a write ahead log sits beside the database.
    bool hasWriteAheadLog=false;
    // The size of that log.
    long long writeAheadLogBytes=0;
    // When the file was last written, in milliseconds since 1970.
    long long modifiedMilliseconds=0;
    // The size of one page, and how many pages the file has.
    long long pageSizeBytes=0;
    long long pageCount=0;
    // Pages the file does not use, and the space they hold.
    long long freePageCount=0;
    long long reclaimableBytes=0;
    // How the database stores changes: "wal", "delete", and so on.
    string journalMode;
    // True when the database hands unused space back by itself.
    bool autoVacuumEnabled=false;
    // True when the compact command understands this agent's event log.
    bool compactionSupported=false;
    // The words the command used when describing the schema it found.
    string schemaDescription;
    // Redundant snapshots in the event log, and their types.
    long long redundantEventRows=0;
    vector<string> redundantEventTypes;
    // The integrity verdict as a word: "intact", "damaged", and so on.
    string integrity;
    // The sentence behind that verdict, which can be empty.
    string integrityMessage;
    // True when the database could be opened at all.
    bool databaseOpenSucceeded=false;
    // The raw code of the failure when it could not, for searching.
    long long openFailureCode=0;
    // What the look cost, which the command reports as a headline number.
    long long durationNanoseconds=0;
    long long metadataCallCount=0;
    // The row counts the command printed, one per table it counted.
    vector<RowCount> rowCounts;
    // What the command found worth saying, and what it suggests doing next.
    vector<string> warnings;
    vector<string> nextSteps;
};
// How far the running command has got, from a `progress` event.
struct ProgressState final {
    // What the command is doing, in its own words.
    string task;
    // Units done and units expected, in the command's own units.
    long long completedUnits=0;
    long long totalUnits=0;
    // True when the command said this was its last progress report.
    bool finished=false;
};
// One named value of a `result` event. The event says whether the value is a
// number, a boolean, or text, so the window never has to guess from the spelling.
struct ResultField final {
    // The field's name, which is also its label in the result dialogue.
    string name;
    // Which of the three value slots below is in use.
    JsonValue::Kind kind=JsonValue::Kind::Text;
    // The value when the kind is text.
    string textValue;
    // The value when the kind is a number.
    long long numberValue=0;
    // The value when the kind is a boolean.
    bool booleanValue=false;
};
// The closing report of a command that finished, when it wrote one.
struct CommandResult final {
    // The command's own name, for example "compact".
    string commandName;
    // The values it reported, in the order it reported them.
    vector<ResultField> fields;
    // True when a result event arrived. Not every command writes one.
    bool isAvailable=false;
};
// One step of a recovery plan.
struct PlanStep final {
    // What the step is called and what it means.
    string title;
    string explanation;
    // The command to run, which the user can copy.
    string command;
    // How urgent the command line considers this step.
    string severity;
    // True when the step writes to the database.
    bool changesData=false;
};
// A whole recovery plan, from a `plan` event.
struct RecoveryPlan final {
    // Which agent's data this concerns, and where it is.
    string agentName;
    string filePath;
    // The steps, in the order the command line put them in.
    vector<PlanStep> steps;
    // True when a plan event arrived.
    bool isAvailable=false;
};
// One conversation, as `sessions list` measured it.
struct SessionEntry final {
    // The session identifier, which is what the export and delete actions take.
    string identifier;
    // The payload the session stores, which is what a deletion would give back.
    long long payloadBytes=0;
    // How many parts the session's messages are stored in.
    long long partCount=0;
};
// The session list of one database. The list belongs to a database rather than to
// the window, because the drawer shows one database's conversations at a time and
// a fresh list must not be shown beside another database's name.
struct SessionList final {
    // The database the list was asked for.
    string databasePath;
    // The conversations, largest first, as the command ranked them.
    vector<SessionEntry> entries;
};
// One table or index inside a database, from an `explain-space` event.
struct SpaceEntry final {
    // The table or index name.
    string name;
    // The bytes it holds.
    long long bytes=0;
    // True when the entry is an index rather than a table.
    bool isIndex=false;
};
// Where one database's bytes are, from an `explain-space` event.
struct SpaceTable final {
    // The database this describes.
    string databasePath;
    // The file's size, its page size, its page count, and the space its free pages
    // hold.
    long long sizeBytes=0;
    long long pageSizeBytes=0;
    long long pageCount=0;
    long long freeBytes=0;
    // The tables and indexes, largest first.
    vector<SpaceEntry> entries;
    // True when a space event arrived.
    bool isAvailable=false;
};
// One database's recorded history, from a `history` event.
struct HistoryRow final {
    // The database the sizes were recorded for.
    string databasePath;
    // How many sizes were recorded, and over how many days they were looked at.
    long long recordedPointCount=0;
    long long windowDays=0;
    // The oldest and newest recorded sizes, and when they were taken. The times are
    // in seconds, which is what the service records.
    long long firstEpochSeconds=0;
    long long firstBytes=0;
    long long lastEpochSeconds=0;
    long long lastBytes=0;
    // The change across the recorded window and the average per day.
    long long changeBytes=0;
    long long bytesPerDay=0;
    // The space free on the drive, and how many days it lasts at this rate. The
    // last number is negative when the command would not guess.
    long long freeBytes=0;
    long long daysUntilDriveFull=-1;
};
// What the background service is doing, from a `service status` event.
struct ServiceStatus final {
    // True when the service is registered with the operating system.
    bool isInstalled=false;
    // True when it is registered and running.
    bool isRunning=false;
    // A sentence describing the current state.
    string stateSentence;
    // Where the configuration and log files are.
    string locations;
    // The most recent line the service wrote to its log, or nothing.
    string lastLogLine;
    // True when a service event arrived.
    bool isAvailable=false;
};
// One supported coding agent, from an `agents` event.
struct AgentRecord final {
    // The agent's name and how it stores its data.
    string name;
    string storage;
    // True when the compact command understands this agent's event log.
    bool supportsCompaction=false;
    // Where the agent's data was found, and where the tool looked for it.
    vector<string> foundPaths;
    vector<string> candidatePaths;
};
// A safety copy that was made, from a `backup` event.
struct BackupRecord final {
    // Where the copy was written.
    string filePath;
    // How large it is.
    long long sizeBytes=0;
};
// What the last scan did.
struct ScanSummary final {
    // True while a scan runs.
    bool isRunning=false;
    // True once a scan has finished at least once.
    bool hasFinished=false;
    // How many databases the last finished scan found.
    int databaseCount=0;
    // When it finished, in milliseconds since 1970, or zero before the first scan.
    long long finishedMilliseconds=0;
};
// The command the window has in flight.
struct RunState final {
    // What the activity bar says, for example "Looking at every database".
    string description;
    // True from the moment the window starts a command until the child has ended
    // and its output has been read.
    bool isActive=false;
};
// The settings the settings tab and the theme menu own.
struct InterfaceSettings final {
    // Which theme is in use: follow the system, dark, or light.
    int themeMode=THEME_FOLLOW_SYSTEM;
    // How much space a reclaim aims to give back, in gigabytes.
    long long reclaimTargetGigabytes=20;
    // True when a safety copy is kept after a successful run.
    bool keepSafetyCopy=false;
    // Where safety copies are written. Empty means beside the database.
    string backupDestinationPath;
    // Where the window was and how large it was when it was last closed, so the
    // next run opens where the user left it.
    int windowX=0;
    int windowY=0;
    int windowWidth=1360;
    int windowHeight=860;
    bool hasWindowPlacement=false;
    // True when the log pane is open. It starts closed: the tabs are what a person
    // came for, and the activity bar already says how the last command ended. One
    // press opens the pane, and the choice is remembered.
    bool logPaneIsOpen=false;
};
// Everything the window knows. One instance lives in the main loop and is passed
// by reference; it holds no lock and no thread, because the reading of the child's
// output happens in the runner and the drawing happens in the screens.
struct GuiState final {
    // Every database the last scan described, in the order the command listed them.
    // A path is described once: a later event replaces the earlier record, which is
    // what makes a second look at one database update it rather than duplicate it.
    vector<DatabaseRecord> databases;
    // What the last scan did.
    ScanSummary scan;
    // The command in flight.
    RunState run;
    // How far it has got.
    ProgressState progress;
    // What it reported and whether it failed.
    CommandResult lastResult;
    bool hasError=false;
    string lastErrorMessage;
    string lastErrorCode;
    // The exit code of the last run that ended, from the `exit` event every machine
    // readable run finishes with. It is the code the interface acts on: the window
    // starts every command in the background so that no console window appears
    // behind it, and SDL reports zero for the exit code of a background process on
    // every platform, so the operating system's own number is only the fallback for
    // a run that wrote no objects at all. When no `exit` event arrived, the command
    // line never got as far as running a command, which is what a mistake in the
    // command line itself looks like.
    bool hasExitCode=false;
    int lastExitCode=0;
    // What the activity bar says while nothing runs: how the last command ended.
    string lastRunEnding;
    // The warnings, the next steps, the safety copies, and the lines of the command
    // in flight. They are cleared when a command starts, so a dialogue never shows
    // one run's numbers beside another run's sentence.
    vector<string> runWarnings;
    vector<string> runNextSteps;
    vector<BackupRecord> runBackups;
    vector<string> runTextLines;
    // The read trace, for a run that counted what it read.
    bool hasReadTrace=false;
    long long readTraceBytes=0;
    long long readTraceCalls=0;
    // The recovery plan of the last plan step.
    RecoveryPlan recoveryPlan;
    // The conversation list of one database.
    SessionList sessionList;
    // Where one database's bytes are.
    SpaceTable spaceTable;
    // The recorded growth of every database that has any.
    vector<HistoryRow> historyRows;
    // What the service is doing.
    ServiceStatus service;
    // The agents the command line supports, when a run listed them.
    vector<AgentRecord> agents;
    // The lines the child produced, oldest first, capped at LOG_LINE_LIMIT.
    vector<string> logLines;
    // What the user chose.
    InterfaceSettings settings;
};
// The wall clock reading in milliseconds since 1970, which is the unit every
// timestamp in the events uses.
long long currentEpochMilliseconds();
// Fills in the state from one parsed event. This is the one place that knows the
// shape of every event the window understands; an event it does not know is
// ignored rather than treated as an error, so a newer command line cannot break
// an older window.
void applyJsonEvent(GuiState& state, const JsonValue& event);
// Appends one line to the log pane, dropping the oldest lines past the limit.
void appendLogLine(GuiState& state, const string& lineText);
// Appends several lines, oldest first.
void appendLogLines(GuiState& state, const vector<string>& lineTexts);
// Starts a command: clears what the previous one published and names this one.
// Called by the interface immediately before the child is started.
void beginRun(GuiState& state, const string& runDescription);
// Starts a whole machine scan. The database list is emptied because the scan is
// about to describe every database afresh, and a half updated mix of two looks
// would be worse than an empty list for the second or so a scan takes.
void beginScan(GuiState& state);
// Marks the command in flight as over and records its exit code. Returns false
// when no command was in flight, which happens when a stopped child produced its
// last line after the run was already closed.
bool endRun(GuiState& state, int exitCode);
// Prepares the conversation list for a fresh listing of one database: the old
// entries belong to the database they were asked for, so they go.
void prepareSessionList(GuiState& state, const string& databasePath);
// Prepares the history table for a fresh reading.
void beginHistoryRefresh(GuiState& state);
// The record for a path, or null when the last scan did not see that database.
const DatabaseRecord* findDatabaseRecord(const GuiState& state, const string& filePath);
// The databases ordered by size, largest first. Files that are not there are left
// out, and ties are broken by path so the order never changes between frames.
vector<const DatabaseRecord*> largestDatabasesFirst(const GuiState& state);
// The total size of every database that was found, and the total space they
// report as reclaimable.
long long totalDatabaseBytes(const GuiState& state);
long long totalReclaimableBytes(const GuiState& state);
// The word the window shows for a database's health.
string healthWord(const DatabaseRecord& record);
// One sentence saying how many databases were found and when the last look
// finished, for the top of the overview tab.
string scanSummarySentence(const GuiState& state, long long nowMilliseconds);
// One sentence saying how a finished command ended, for the top of its dialogue.
// `wasCancelled` is true when the user stopped it, which is not a failure and is
// not described as one.
string runVerdictSentence(const GuiState& state, int exitCode, bool wasCancelled);
// One result field as it is shown in the result dialogue: a byte count for a
// field whose name says it is one, thousands separators for the other numbers,
// "yes" and "no" for booleans.
string formatResultFieldValue(const ResultField& field);
// A byte count in the units a person reads, with one decimal place below a
// terabyte, exactly as the command line writes it.
string formatBytesForWindow(long long byteCount);
// A count with thousands separators.
string formatCountForWindow(long long count);
// A timestamp in the form the command line uses: 2026-10-18 14:22:31.
string formatTimestampForWindow(long long epochMilliseconds);
// A timestamp as how long ago it was, in the largest unit that fits.
string formatAgeForWindow(long long epochMilliseconds, long long nowMilliseconds);
// Turns the settings into the text the settings file holds, and back. The format
// is one `name=value` pair per line, so a person can read and edit the file, and
// a line the reader does not know is ignored rather than treated as damage. A
// value that makes no sense is replaced by the setting's own default rather than
// failing, so a damaged file costs one setting and not the whole run.
string serializeSettings(const InterfaceSettings& settings);
void parseSettings(const string& settingsText, InterfaceSettings& settingsOut);
// One command line invocation: the subcommand, the positional action, and every
// option the window ever passes.
struct CliInvocation final {
    // The subcommand: "diagnose", "compact", "vacuum", "recover", "reclaim",
    // "sessions", "export", "history", "service", "backups", "explain-space", or
    // "agents". Always set.
    string command;
    // The positional action the subcommand takes, when it takes one: "list" or
    // "delete" for sessions, "list", "verify", "restore", or "delete" for backups,
    // and the action for service.
    string action;
    // The database to work on, for -d.
    string databasePath;
    // The conversation to work on, for --session.
    string sessionIdentifier;
    // Where a report or an export is written, for -o.
    string outputPath;
    // The export format: "markdown", "json", or "html".
    string exportFormat;
    // Where a safety copy is written, for --backup-to.
    string backupDestinationPath;
    // The copy a backups action works on, for --file.
    string backupsFilePath;
    // How much space a reclaim aims for, for --target, in bytes. Zero leaves the
    // command's own default in place.
    long long reclaimTargetBytes=0;
    // What the service watches for, for --warning-threshold, in bytes.
    long long warningThresholdBytes=0;
    // How often the service checks, for --interval, in seconds.
    long long intervalSeconds=0;
    // How many recent conversations a compaction keeps, for -k. Negative leaves the
    // command's own default in place.
    long long sessionsToKeep=-1;
    // The largest share of the event table a compaction may remove, for
    // --max-fraction. Negative leaves the command's own default in place.
    double maximumFractionToRemove=-1.0;
    // How many conversations a listing shows, for --top, and how many days of
    // recorded sizes the history covers, for --days.
    long long sessionLimit=0;
    long long historyDays=0;
    // How old a copy must be before a deletion removes it, for --older-than.
    // Negative means no age was named.
    long long olderThanDays=-1;
    // True to keep the safety copy after a successful run, for --keep-backup.
    bool keepSafetyCopy=false;
    // True to pre-answer the command's own confirmation, for --yes.
    bool answerYes=false;
    // True to change nothing and report what would happen, for --dry-run.
    bool dryRun=false;
    // True to print only the lines that matter, for --quiet.
    bool quiet=true;
    // True to run the full page by page integrity check, for --check.
    bool integrityCheck=false;
    // True to count the bytes the run read, for --trace-reads.
    bool traceReads=false;
    // True to convert the database to incremental auto-vacuum, for
    // --enable-auto-vacuum.
    bool enableAutoVacuum=false;
    // True to act on every copy rather than the newest, for --all.
    bool backupsAll=false;
    // True to also list the paths the tool looked in, for --verify.
    bool verifyCandidatePaths=false;
    // True to build a working copy of a damaged database, for --rebuild.
    bool rebuildWorkingCopy=false;
};
// The argument vector for an invocation, without the program's own name. Every
// run is asked for machine readable output, because the window reads the objects
// and not the prose; the human lines still arrive, and the log pane shows them.
vector<string> buildCommandArguments(const CliInvocation& invocation);
}
#endif
