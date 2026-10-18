// The SessionsCommand class, which lists conversations and deletes one of them.
//
// Deleting a session is the sharpest thing this tool can do: it removes a
// conversation nothing else holds. It is therefore gated the same way the event log
// compaction is: the file is copied first, the question demands a typed yes, the
// deletion runs in one transaction, and a sample of the other sessions is compared
// before and after. When the checked comparison fails the copy goes back.
//
// The command refuses on a database whose second generation tables are in use,
// because their layout is not documented anywhere this tool trusts, and deleting a
// session whose newer rows live in those tables would leave the agent pointing at
// something that is half gone.
#ifndef AGENT_DB_DOCTOR_COMMAND_SESSIONSCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_SESSIONSCOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The SessionsCommand class lists sessions and removes one after a copy and a check.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class SessionsCommand final {
public:
    // Builds the command with its own reporter.
    explicit SessionsCommand(bool isTerminalAttached);
    // Runs the action named in the options and returns the process exit code.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
private:
    int listSessions(const CommandOptions& options);
    int deleteSession(const CommandOptions& options);
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
};
}
#endif
