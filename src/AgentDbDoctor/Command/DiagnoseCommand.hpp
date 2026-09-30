// The DiagnoseCommand class, which answers the question a worried user asks first:
// what is actually going on with my agent database?
//
// The command never writes to the database. It is safe to run at any time, against
// a database that is currently open, and it costs the number of metadata calls the
// performance contract allows rather than a full table scan.
#ifndef AGENT_DB_DOCTOR_COMMAND_DIAGNOSECOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_DIAGNOSECOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides the AgentDatabaseDoctor class, which runs the pass.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The DiagnoseCommand class runs the diagnostic pass and prints the result.
//
// The class is `final` because the specification forbids inheritance. It holds an
// AgentDatabaseDoctor and a ConsoleReport as fields and calls methods on them, which
// is the composition the specification requires in place of a common base class.
class DiagnoseCommand final {
public:
    // Builds the command with its own reporter, using the terminal flag so that no
    // escape sequence is written when the output is a file.
    explicit DiagnoseCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code, where zero means nothing
    // needs attention and one means something does.
    //
    // The method never lets a damaged or missing database become a crash. Those are
    // results, not errors, and the specification requires them to be explained in
    // plain language rather than reported as a failure code.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list, which is also the word the user types.
    string commandName() const;
    // Prints the sentence that says what the command is about to do. Every command
    // prints this before it acts, as the specification requires.
    void writePurpose(const CommandOptions& options) const;
    // The number of databases the command found nothing wrong with, out of how many
    // it looked at. The command uses it to choose an exit code.
    int healthyDatabaseCount() const;
    // The number of databases the command found a problem with.
    int problemDatabaseCount() const;
    // Resets the two counters above. Called at the start of every run.
    void resetCounts();
private:
    // Finds the databases to report on, either the one the user named or every one
    // the supported agents know about. The agent names are written into
    // `agentNamesOut` so the caller can label each report.
    vector<string> resolveDatabasePaths(
        const CommandOptions& options,
        vector<string>& agentNamesOut
    ) const;
    // Holds the reporter by value so that every line the command prints goes
    // through the one class that is allowed to print it.
    ConsoleReport reporter;
    // The doctor that runs the pass. It holds a registry of the supported agents and
    // no state between calls, so owning one costs nothing.
    AgentDatabaseDoctor doctor;
    int healthyCount=0;
    int problemCount=0;
};
}
#endif
