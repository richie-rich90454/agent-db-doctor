// The ExplainSpaceCommand class, which says where a database's bytes actually are.
//
// The header of a SQLite file reports how large it is and how many pages are free.
// It says nothing about which table or index holds the rest, and that is the
// question a person looking at a forty gigabyte file is really asking. This command
// answers it from SQLite's own page accounting, and with `--sessions` it also names
// the largest conversations, which is the amount people are usually willing to part
// with.
#ifndef AGENT_DB_DOCTOR_COMMAND_EXPLAINSPACECOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_EXPLAINSPACECOMMAND_HPP
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
// The ExplainSpaceCommand class reports how the bytes in one database are divided.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class ExplainSpaceCommand final {
public:
    // Builds the command with its own reporter.
    explicit ExplainSpaceCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code: zero when the measurement
    // was made, one when it could not be.
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
