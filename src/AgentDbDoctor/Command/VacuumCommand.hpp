// The VacuumCommand class, which returns unused space inside a database to the
// drive.
//
// The vacuum is done with SQLite's own `VACUUM INTO`, which writes a brand new
// database containing only the pages still in use and then swaps it into place.
// The tool never modifies the original in place, and it never uses file system
// compression, because the specification is explicit that compressing a SQLite
// file does not shrink it and can make it unreadable.
#ifndef AGENT_DB_DOCTOR_COMMAND_VACUUMCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_VACUUMCOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides the guard that copies the file and asks the user.
#include "AgentDbDoctor/Command/DestructiveOperationGuard.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The VacuumOutcome value type says what a vacuum run did.
struct VacuumOutcome final {
    // True when nothing was written because this was a preview.
    bool wasPreviewOnly=false;
    // True when the file was replaced with a smaller one.
    bool wasCompacted=false;
    // The size before, in bytes.
    long long sizeBeforeBytes=0;
    // The size after, in bytes.
    long long sizeAfterBytes=0;
    // The number of bytes given back to the drive.
    long long bytesReclaimed=0;
    // The free space the operation needed while it was running, which is what a
    // user needs to know before starting one on a nearly full drive.
    long long peakFreeSpaceRequiredBytes=0;
    // A sentence describing the outcome.
    string summarySentence;
    // True when the run stopped because there was not enough free space.
    bool wasRefusedForSpace=false;
    // True when the run stopped because the database could not be read.
    bool wasRefusedForDamage=false;
    // True when the run stopped because of anything else it refused to do, which
    // the command line reports as exit code one rather than as success.
    bool wasRefused=false;
    // The bytes of the safety copy that were kept rather than removed. A kept copy
    // is space the drive did not get back, so it is reported beside the reclaimed
    // bytes instead of hiding inside them.
    long long keptSafetyCopyBytes=0;
    // The net change in free space the drive saw, counting the safety copy. This is
    // the number the user actually cares about: the file shrinking by one gigabyte
    // while a two gigabyte copy stays behind costs the drive a gigabyte.
    long long netBytesFreed=0;
};
// The VacuumCommand class rebuilds a database at its smallest useful size.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport and a DestructiveOperationGuard as fields.
class VacuumCommand final {
public:
    // Builds the command with its own reporter.
    explicit VacuumCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
    // Prints the sentence that says what the command is about to do, including how
    // much free space the operation will need, before it starts.
    void writePurpose(const CommandOptions& options) const;
    // The result of the last run.
    const VacuumOutcome& lastOutcome() const;
private:
    // Runs the whole operation, so that the decision and the act are separate.
    VacuumOutcome performVacuum(const CommandOptions& options, const string& databaseFilePath);
    // Resolves a swap a previous run left half finished, before this run starts.
    // A power loss between any two renames is what the journal written just before
    // them exists for, and this is the other half of that contract.
    void resolveInterruptedVacuumSwap(const string& databaseFilePath) const;
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
    // The outcome of the last run.
    VacuumOutcome lastRunOutcome;
};
}
#endif
