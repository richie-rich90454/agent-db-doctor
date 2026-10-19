// The AuditCommand class, which shows what the tool changed by itself.
//
// The service can compact and vacuum a database without anyone watching, and the
// audit log is where those runs are recorded. This command is the view of that
// record: one line per step, with the time, the database, the size it changed
// from and to, and the outcome in the tool's own words. It only reads, so it is
// safe to run at any time, including while the service is working.
#ifndef AGENT_DB_DOCTOR_COMMAND_AUDITCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_AUDITCOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides the AuditEntry value type this command prints.
#include "AgentDbDoctor/Service/AuditLog.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Service;
namespace AgentDbDoctor::Command {
// The AuditCommand class prints the record of the service's own changes.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of a common base class.
class AuditCommand final {
public:
    // Builds the command with its own reporter, using the terminal flag so that no
    // escape sequence is written when the output is a file.
    explicit AuditCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code. The command cannot fail
    // in a way that matters: an empty log is a result and exits zero, and a log
    // that cannot be read is said so in a sentence and also exits zero.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list, which is also the word the user types.
    string commandName() const;
private:
    // Writes one entry as a single line for a person: the time, the action, the
    // path, the size change, and the outcome.
    void writeEntryLine(const AuditEntry& entry) const;
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
};
}
#endif
