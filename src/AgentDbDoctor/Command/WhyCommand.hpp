// The WhyCommand class, which answers one question in one place: why is this
// database this large, and what would make it smaller?
//
// The other commands answer parts of that: diagnose reports the size and the health,
// explain-space divides the bytes between tables, history reports the growth. This
// command runs the cheap parts of all three, ranks the causes by size, and ends
// with the one command worth running next. It never opens the database for writing
// and never scans the conversation; `--sessions` is the one exception and says so.
#ifndef AGENT_DB_DOCTOR_COMMAND_WHYCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_WHYCOMMAND_HPP
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
// The WhyCommand class explains one database's size.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class WhyCommand final {
public:
    // Builds the command with its own reporter.
    explicit WhyCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code.
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
