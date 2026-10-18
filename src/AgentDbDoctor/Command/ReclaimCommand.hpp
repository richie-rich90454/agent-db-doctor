// The ReclaimCommand class, the one command for the job people actually have: give
// me this much space back.
//
// People think in gigabytes, not in operations. The reclaim command takes a target,
// works out the cheapest safe way to reach it — compaction first, because it is what
// makes the vacuum smaller, then the vacuum — shows the plan, asks once, runs both
// with the full safety machinery each, and reports what the drive actually got back.
#ifndef AGENT_DB_DOCTOR_COMMAND_RECLAIMCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_RECLAIMCOMMAND_HPP
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
// The ReclaimCommand class reaches a target of freed space in as few steps as it can.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class ReclaimCommand final {
public:
    // Builds the command with its own reporter.
    explicit ReclaimCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
    // Parses a target such as `20GB`, `500MB`, or a plain number of bytes. Returns
    // zero when the text is not a size, which the caller reports rather than
    // guessing at.
    static long long parseTargetBytes(const string& targetText);
private:
    // The confirmation used before the run, with the same exact word the rest of the
    // tool demands.
    bool confirmWithUser(const CommandOptions& options, const string& questionText) const;
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
};
}
#endif
