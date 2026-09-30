// The ServiceCommand class, which installs, controls, and reports on the system
// service.
//
// The service is the answer to the problem the specification tells the story about.
// A tool that only runs when a person remembers to run it will be run after the
// disk is full; a tool that watches costs one metadata call an hour and says
// something while there is still time to act.
#ifndef AGENT_DB_DOCTOR_COMMAND_SERVICECOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_SERVICECOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides the ServiceManager class, which does the platform work.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Service;
namespace AgentDbDoctor::Command {
// The ServiceCommand class installs and controls the background service.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and a ServiceManager by pointer, because the manager
// only exists once a configuration has been read.
class ServiceCommand final {
public:
    // Builds the command with its own reporter.
    explicit ServiceCommand(bool isTerminalAttached);
    // Runs the command and returns the process exit code.
    //
    // `options.serviceAction` says which of the six things to do: `install`,
    // `uninstall`, `start`, `stop`, `status`, or `run`. The first five print
    // something and return; the last one enters the watchdog loop and does not
    // return, because it is the body of the service process.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
    // The six actions the command understands, for the help text.
    static vector<string> supportedActions();
private:
    // Builds a configuration from the options, reading an existing file when there
    // is one and falling back to the defaults when there is not.
    ServiceConfiguration buildConfiguration(const CommandOptions& options) const;
    // Prints the lines explaining what installing will do and where things go.
    void writeInstallExplanation(const ServiceConfiguration& configuration) const;
    ConsoleReport reporter;
};
}
#endif
