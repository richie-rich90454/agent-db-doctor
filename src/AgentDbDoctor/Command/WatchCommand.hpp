// The WatchCommand class, a live table of every database, its size, and its growth.
//
// This is the terminal's answer to opening a window: a refreshing list, sorted by
// size, with the growth since the last refresh beside each file. It is deliberately
// the cheapest possible view — the sizes come from file metadata, and no database is
// ever opened — so it can run on a second monitor for days without touching the
// user's data or costing anything worth noticing.
#ifndef AGENT_DB_DOCTOR_COMMAND_WATCHCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_WATCHCOMMAND_HPP
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
// The WatchCommand class renders the live table.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class WatchCommand final {
public:
    // Builds the command with its own reporter.
    explicit WatchCommand(bool isTerminalAttached);
    // Runs the command: one snapshot with `--once`, otherwise until the process is
    // stopped. Returns the exit code.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
private:
    // Renders one snapshot and returns the total bytes it saw.
    long long renderOneSnapshot();
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
};
}
#endif
