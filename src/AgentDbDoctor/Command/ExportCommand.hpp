// The ExportCommand class, which writes one session out as a readable document.
//
// Compaction and vacuuming are safe because the conversation lives in the session,
// message, and part tables, but "safe" is not the same as "backed up". This command
// is the way to take a copy of one conversation in a form that any text editor can
// open: before a deletion, before leaving an agent, or before trusting a damaged
// database to a rebuild. It reads only, and it writes only to the file it is told
// to write to.
#ifndef AGENT_DB_DOCTOR_COMMAND_EXPORTCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_EXPORTCOMMAND_HPP
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
// The ExportCommand class writes one session to Markdown, JSON, or HTML.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class ExportCommand final {
public:
    // Builds the command with its own reporter.
    explicit ExportCommand(bool isTerminalAttached);
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
