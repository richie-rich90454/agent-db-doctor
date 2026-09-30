// The RecoverCommand class, which walks a person through what to do when their
// agent database will not open.
//
// This command exists because of a specific, real loss. A developer compressed a
// live database with an algorithm that could not compress its pages, ran out of
// disk space, rebooted mid write, and ended up with a file that no tool could
// read. Every step of that story is recognised here by name, and the first thing
// the command does is tell the user to stop touching the disk, because that is the
// one piece of advice that would have saved the sessions.
#ifndef AGENT_DB_DOCTOR_COMMAND_RECOVERCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_RECOVERCOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides the guard that copies the file and asks the user.
#include "AgentDbDoctor/Command/DestructiveOperationGuard.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Recovery;
namespace AgentDbDoctor::Command {
// The RecoverOutcome value type says what a recovery run did.
struct RecoverOutcome final {
    // The internal name of the pattern that was recognised, or the empty string
    // when nothing recognisable was found.
    string matchedPatternName;
    // The sentence explaining what happened, in plain English.
    string plainExplanation;
    // True when this tool can make the situation better on its own.
    bool isRecoverableByThisTool=false;
    // True when the file's own bytes are gone, so nothing can bring them back. The
    // specification insists the tool says this rather than let a user keep trying.
    bool dataIsUnrecoverable=false;
    // The number of rows the rebuild recovered into the new database, or zero when
    // no rebuild was performed.
    long long recoveredRowCount=0;
    // The path of the rebuilt database, when one was made.
    string rebuiltDatabasePath;
    // A sentence describing what was done.
    string summarySentence;
};
// The RecoverCommand class diagnoses a database that will not open and offers the
// safe path forward.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport and a DestructiveOperationGuard as fields.
class RecoverCommand final {
public:
    // Builds the command with its own reporter.
    explicit RecoverCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code, where zero means a plan
    // was printed and one means the tool could not even read the file.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
    // The result of the last run.
    const RecoverOutcome& lastOutcome() const;
    // Builds the plan for a database without running anything. Exposed so the
    // diagnose command can show the plan without the user having to ask twice.
    vector<RecoveryRecommendation> buildPlanFor(const CommandOptions& options) const;
private:
    // Rebuilds the indexes of a damaged but readable database, working on a copy.
    RecoverOutcome rebuildIndexes(const CommandOptions& options, const string& databaseFilePath);
    // Rebuilds a fresh database from the rows SQLite can still read, working on a
    // copy.
    RecoverOutcome rebuildFromDump(const CommandOptions& options, const string& databaseFilePath);
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
    // The outcome of the last run.
    RecoverOutcome lastRunOutcome;
};
}
#endif
