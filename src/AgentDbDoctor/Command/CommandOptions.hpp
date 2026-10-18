// The CommandOptions value type, which holds every setting a command accepts.
//
// The specification requires that every command print what it is about to do and
// wait for confirmation unless an explicit flag was given. Rather than repeat that
// logic in five command classes, the options live in one value type and the shared
// behaviour lives in a class that contains it. That is composition, which is what
// the specification requires in place of a common base class.
#ifndef AGENT_DB_DOCTOR_COMMAND_COMMANDOPTIONS_HPP
#define AGENT_DB_DOCTOR_COMMAND_COMMANDOPTIONS_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ProgressSink vocabulary, so a graphical front end can receive
// the same progress numbers the console bar draws.
#include "AgentDbDoctor/Presentation/ProgressSink.hpp"
using namespace std;
namespace AgentDbDoctor::Command {
// The CommandOptions value type holds the settings one run of one command uses.
//
// It is a plain value with public fields, returned by value. There is no class
// above it, because the specification forbids inheritance.
struct CommandOptions final {
    // The database to act on. The empty string means "find it yourself", which is
    // the default for every command.
    string databaseFilePath;
    // The name of the agent whose data is wanted. The empty string means every
    // supported agent.
    string agentName;
    // The number of the most recent sessions to keep. Zero means the tool's own
    // default. This is the only setting that can lose data, and the tool refuses to
    // act on it without a confirmation.
    int sessionsToKeep=0;
    // True when the user has already answered yes, so the tool does not ask again.
    // The specification requires this flag to exist for unattended runs such as the
    // system service.
    bool answerYesToEverything=false;
    // True when nothing should be written, only reported. Every command honours it,
    // and the service always sets it.
    bool dryRunOnly=false;
    // True when the file system compression the specification warns about should be
    // removed from the database. The tool never applies compression itself, so the
    // only way to end up in that state is a user who did it by hand.
    bool removeFileSystemCompression=false;
    // True when the quick integrity check should run. It is off by default because
    // it reads the whole file, which is exactly the cost the tool is built to avoid.
    bool runIntegrityCheck=false;
    // True when the recover command should build a working copy of a damaged
    // database. It is off by default so that an exploratory run writes nothing, and
    // the original file is never modified either way.
    bool rebuildWorkingCopy=false;
    // True when the report should be printed as machine readable text rather than
    // for a person. The shape is documented in the README.
    bool machineReadableOutput=false;
    // True when the run should also report how many bytes were read from the
    // databases. The counters are always kept; this only prints them.
    bool traceReadCounts=false;
    // True when `agents` should also show the candidate paths that were looked at,
    // which is where each agent's data is expected and what was not found.
    bool verifyAgentPaths=false;
    // How many of the largest sessions `explain-space` should rank. Zero means the
    // ranking was not asked for, which is the default because measuring it reads the
    // stored payload.
    int sessionsToRank=0;
    // How many days of growth history a report should cover. Zero means the default.
    int historyDaysToShow=0;
    // The output format for sessions being exported: markdown, json, or html.
    string exportFormat="markdown";
    // The session to export, or to delete. The empty string means all of them for an
    // export and nothing at all for a deletion, which is refused.
    string sessionIdentifier;
    // Where an export or an HTML report is written. A single dash means standard
    // output.
    string outputFilePath;
    // The target of a reclaim run, for example 20GB. The empty string means no
    // target was given.
    string reclaimTarget;
    // True when a vacuum should convert the database to incremental auto-vacuum, so
    // that later reclaims are cheap, at the cost of one full rewrite now.
    bool enableIncrementalAutoVacuum=false;
    // True when progress lines should be suppressed, for use in a script.
    bool quiet=false;
    // True when extra detail should be written to the log.
    bool verbose=false;
    // The number of rows a single deletion transaction removes. Ten thousand is the
    // value the specification names: large enough that the flush cost is amortised,
    // small enough that the undo log stays small.
    int deletionBatchSize=10000;
    // The highest fraction of the database a single compaction run is allowed to remove.
    // The tool refuses rather than making a change this large without a human
    // deciding, which protects against a misread row count.
    double maximumFractionToRemove=0.5;
    // The path a safety copy is written to. The empty string means the tool picks a
    // name beside the original.
    string backupFilePath;
    // True when the safety copy should be kept even after the run finished and
    // passed its checks. The default is to remove it then, because a copy of a
    // hundred gigabyte database would otherwise cancel out the space the run
    // reclaimed. Naming a destination with `--backup-to` keeps the copy too: a
    // location the user chose is a location the user still wants.
    bool keepBackup=false;
    // The number of bytes a database must reach before the service says something.
    // The default is fifty gigabytes, which is where a disk starts to become a
    // problem on a typical machine.
    long long warningThresholdBytes=53687091200LL;
    // How many seconds the service waits between checks. One hour matches the
    // specification's note that the service logs at most one line an hour, so the
    // tick cost is never worth increasing the frequency for.
    int watchdogIntervalSeconds=3600;
    // The service subcommand: install, uninstall, start, stop, status, or run.
    string serviceAction;
    // The backups subcommand: list, restore, delete, or verify. The empty string
    // means list, which is the harmless default.
    string backupsAction;
    // True when every backup for the target should be removed rather than only the
    // ones older than a number of days.
    bool backupsDeleteAll=false;
    // How old a backup must be, in days, before `backups delete` removes it. A
    // negative value means no age limit was given, which is refused unless --all
    // says the whole set is meant.
    int backupsOlderThanDays=-1;
    // A specific backup file for `backups restore` or `backups verify`. The empty
    // string means the newest copy beside the database.
    string backupsFilePath;
    // Receives progress reports from long operations. Empty on the command
    // line, where the console bar draws them instead; a graphical front end
    // sets one and feeds its own widgets from it.
    AgentDbDoctor::Presentation::ProgressSink progressSink;
};
}
#endif
