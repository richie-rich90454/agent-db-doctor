// The HistoryCommand class, which turns the watchdog's size record into a trend and
// a forecast.
//
// A single size says how bad things are. The trend says how bad they are getting,
// which is the number that decides whether anything has to be done this month. The
// forecast is deliberately simple arithmetic and says so: a straight line through
// the recorded sizes, extended to the point where the drive would be full.
#ifndef AGENT_DB_DOCTOR_COMMAND_HISTORYCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_HISTORYCOMMAND_HPP
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
// The HistoryCommand class reports how the databases have grown.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class HistoryCommand final {
public:
    // Builds the command with its own reporter.
    explicit HistoryCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code. Recording is the
    // watchdog's job, so this command only reads and never fails over an empty
    // record.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
private:
    // Reports one database's trend, and returns true when there was anything to
    // report.
    bool reportOneDatabase(const string& databaseFilePath, int windowDays);
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
};
}
#endif
