// The CompactCommand class, which performs event log compaction.
//
// The agent writes a full snapshot of a message into its event log every time the
// message changes. Compaction keeps the newest snapshot and removes the older
// copies, without changing the logical content of the log: the sessions, the
// messages, and the stored parts are untouched.
//
// This is the only command in the tool that deletes rows, and it is written so
// that it cannot lose a conversation. The decision phase proves that the message
// text lives outside the rows it is about to remove, copies the file, and counts
// the change; the compactor then samples the oldest sessions, asks permission,
// deletes in batches, and checks the sample again.
#ifndef AGENT_DB_DOCTOR_COMMAND_COMPACTCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_COMPACTCOMMAND_HPP
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the compactor, the plan, and the report this command produces.
#include "AgentDbDoctor/Command/EventLogCompactor.hpp"
// Provides the Database wrapper the decision phase reads from.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The CompactCommand class compacts the event log and proves nothing was lost.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and creates an EventLogCompactor for the act itself,
// which is the composition the specification requires in place of deriving.
class CompactCommand final {
public:
    // Builds the command with its own reporter.
    explicit CompactCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code, where zero means the
    // run finished and one means nothing was changed.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
    // Prints the sentence that says what the command is about to do.
    void writePurpose(const CommandOptions& options) const;
    // The result of the last run, so a caller can report it without parsing text.
    const EventLogCompactionReport& lastOutcome() const;
    // The share of the event table above which the tool refuses to run without an
    // explicit session count, because a change that large is more likely to be a
    // misread than a real problem.
    static constexpr double DEFAULT_MAXIMUM_FRACTION_TO_REMOVE=0.5;
private:
    // The decision phase: runs every safety check and counts what would be
    // removed. Returns nothing when the run must stop, in which case the report
    // already carries the sentence that says why.
    optional<EventLogCompactionPlan> buildPlan(
        const CommandOptions& options,
        Database& database,
        EventLogCompactionReport& reportOut
    );
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
    // The outcome of the last run, kept so a caller can read it as data rather than
    // parsing the printed text.
    EventLogCompactionReport lastRunOutcome;
};
}
#endif
