// The AgentsCommand class, which lists every supported coding agent and shows where
// its data was found on this machine.
//
// The tool supports sixty two agents, and a user who wonders whether their agent is
// among them, or where its data actually lives, should not have to read the source
// code. With `--verify` the command also shows the candidate paths that were looked
// at and found nothing, which is both a map of where each agent keeps its data and
// the honest answer when an agent is installed somewhere the tool does not know.
#ifndef AGENT_DB_DOCTOR_COMMAND_AGENTSCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_AGENTSCOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The AgentsCommand class reports the supported agents and their discovered data.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class AgentsCommand final {
public:
    // Builds the command with its own reporter.
    explicit AgentsCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code, which is zero whatever was
    // found: listing agents is a question, not an operation, and having none
    // installed is not a failure.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
private:
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
};
}
#endif
