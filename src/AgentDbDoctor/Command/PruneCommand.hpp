// The PruneCommand class, which removes the duplicate rows an agent leaves behind.
//
// This is the only command in the tool that deletes rows, and it is written so that
// it cannot lose a conversation. Before anything is deleted it proves that the
// message text lives outside the rows it is about to remove, it copies the file,
// it samples the oldest sessions, and after every batch it checks again that those
// sessions still hold the same text.
#ifndef AGENT_DB_DOCTOR_COMMAND_PRUNECOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_PRUNECOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
#include "AgentDbDoctor/Command/DestructiveOperationGuard.hpp"
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The PruneOutcome value type says what a prune run did.
struct PruneOutcome final {
    // True when rows were removed.
    bool anythingWasRemoved=false;
    // The number of rows removed.
    long long removedRowCount=0;
    // The number of rows the tool found and deliberately left alone.
    long long protectedRowCount=0;
    // The number of sessions left untouched because they are too new.
    long long protectedSessionCount=0;
    // The number of sessions that were checked afterwards and still hold their text.
    long long verifiedSessionCount=0;
    // The bytes a compaction would now be able to give back.
    long long reclaimableBytesNow=0;
    // A sentence describing the outcome, used as the closing line of the report.
    string summarySentence;
    // True when the run stopped early because verification failed, in which case the
    // safety copy was put back and nothing was lost.
    bool wasRolledBack=false;
};
// The PruneCommand class removes known duplicate rows and proves nothing was lost.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport, a SafetyChecks, and a DestructiveOperationGuard as fields.
class PruneCommand final {
public:
    // Builds the command with its own reporter.
    explicit PruneCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code, where zero means the
    // duplicate rows were removed and one means nothing was changed.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
    // Prints the sentence that says what the command is about to do.
    void writePurpose(const CommandOptions& options) const;
    // The result of the last run, so a caller can report it without parsing text.
    const PruneOutcome& lastOutcome() const;
    // How many of the oldest sessions are checked before and after the deletion.
    // A sample of ten is enough to catch a mistake that affects everything, and
    // small enough that the check costs nothing.
    static constexpr int VERIFIED_SESSION_SAMPLE_SIZE=10;
    // The share of the event table above which the tool refuses to run without an
    // explicit session count, because a change that large is more likely to be a
    // misread than a real problem.
    static constexpr double DEFAULT_MAXIMUM_FRACTION_TO_REMOVE=0.5;
    // The number of rows in one deletion transaction.
    static constexpr int DEFAULT_DELETION_BATCH_SIZE=10000;
private:
    // Runs the whole operation. Returns the outcome rather than printing, so the
    // decision of whether to delete is separate from the act of deleting.
    PruneOutcome performPrune(const CommandOptions& options, const string& databaseFilePath);
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
    // The outcome of the last run, kept so a caller can read it as data rather than
    // parsing the printed text.
    PruneOutcome lastRunOutcome;
};
}
#endif
