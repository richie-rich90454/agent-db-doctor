// The EventLogCompactor class, which performs the event log compaction that a
// plan describes.
//
// Compaction is the operation that keeps the newest copy of every message
// snapshot in an append-only event log and removes the older copies, without
// changing the logical content of the log. The plan says what the compaction
// will do, and it is decided, checked, and counted before this class is called;
// this class only carries the plan out, either as a preview that changes
// nothing or for real.
#ifndef AGENT_DB_DOCTOR_COMMAND_EVENTLOGCOMPACTOR_HPP
#define AGENT_DB_DOCTOR_COMMAND_EVENTLOGCOMPACTOR_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the Database wrapper this class works on.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
using namespace std;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The EventLogCompactionPlan says what a compaction will do.
//
// It is produced by the decision phase, which runs every safety check and counts
// what would be removed before anything is touched. Carrying it as a value means
// the same plan can be shown to the user, run for real, or both.
struct EventLogCompactionPlan final {
    // The event type names that mark a row as an older copy. They come from the
    // agent's own descriptor, which is the single place that names them.
    vector<string> redundantEventTypes;
    // How many of the most recent sessions stay untouched.
    int sessionsToKeep=0;
    // The number of sessions in the database, for the report.
    long long totalSessionCount=0;
    // The number of rows the plan would remove.
    long long doomedRowCount=0;
};
// The EventLogCompactionReport says what a compaction did, or, in preview mode,
// what it would do.
struct EventLogCompactionReport final {
    // True when nothing was written because this was a preview.
    bool wasPreviewOnly=false;
    // True when rows were removed.
    bool anythingWasRemoved=false;
    // The number of rows removed.
    long long removedRowCount=0;
    // The number of sessions left untouched because they are too new.
    long long protectedSessionCount=0;
    // The number of sessions that were checked afterwards and still hold their text.
    long long verifiedSessionCount=0;
    // The bytes a vacuum would now be able to give back.
    long long reclaimableBytesNow=0;
    // A sentence describing the outcome, used as the closing line of the report.
    string summarySentence;
    // True when the run stopped early because verification failed, in which case the
    // safety copy was put back and nothing was lost.
    bool wasRolledBack=false;
};
// The mode the compactor runs in.
enum class CompactionMode {
    // Compute nothing further and change nothing: the report describes what an
    // execute would do. A preview never writes, never asks, and never copies.
    Preview,
    // Perform the compaction the plan describes, after a safety copy and a
    // confirmation.
    Execute
};
// The EventLogCompactor class carries out an event log compaction.
//
// The class is `final` because the specification forbids inheritance. It holds
// the console report by reference and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class EventLogCompactor final {
public:
    // Builds a compactor that prints through the given report.
    explicit EventLogCompactor(const ConsoleReport& reporter);
    // Runs the plan in the given mode and returns what happened.
    //
    // The database must already be open. In `Execute` mode the run makes a
    // safety copy, asks for confirmation, removes the rows in batches, and
    // re-reads the oldest sessions afterwards; a failed check puts the safety
    // copy back. In `Preview` mode nothing is written at all.
    EventLogCompactionReport run(
        CompactionMode mode,
        const CommandOptions& options,
        Database& database,
        const EventLogCompactionPlan& plan,
        const string& databaseFilePath
    );
    // How many of the oldest sessions are checked before and after the deletion.
    // A sample of ten is enough to catch a mistake that affects everything, and
    // small enough that the check costs nothing.
    static constexpr int VERIFIED_SESSION_SAMPLE_SIZE=10;
    // The number of rows in one deletion transaction.
    static constexpr int DEFAULT_DELETION_BATCH_SIZE=10000;
private:
    // Every line the compactor prints goes through the report, which is the only
    // class in the tool allowed to write to the user's screen.
    const ConsoleReport& reporter;
};
}
#endif
