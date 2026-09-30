// The implementation of the ServiceCommand class.
//
// The command never installs anything without printing first what it is about to
// do and where the files will go, and it never starts a service that is already
// running, because both surprises are the kind this tool is meant to avoid.
#include <iostream>
// Provides `cin` and `getline`, which read the confirmation from standard input.
#include <iostream>
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ServiceCommand class declaration.
#include "AgentDbDoctor/Command/ServiceCommand.hpp"
// Provides the ConfigurationException and the ServiceOperationException.
#include "AgentDbDoctor/Common/Exceptions.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Service;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
ServiceCommand::ServiceCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string ServiceCommand::describe() const {
    return string(
        "Watch the supported coding agent databases in the background and write a line to a log "
        "when one grows too large. The check costs one file system query per database and reads "
        "nothing from them."
    );
}
string ServiceCommand::commandName() const {
    return string("service");
}
vector<string> ServiceCommand::supportedActions() {
    return vector<string>{
        string("install"),
        string("uninstall"),
        string("start"),
        string("stop"),
        string("status"),
        string("run")
    };
}
ServiceConfiguration ServiceCommand::buildConfiguration(const CommandOptions& options) const {
    // An existing configuration file is the starting point, so that a user who has
    // changed a threshold and then reinstalls does not lose it. A missing file is
    // not an error, because that is a first run.
    const string configurationPath=ConfigurationFile::defaultFilePath();
    optional<ServiceConfiguration> existingConfiguration=ConfigurationFile::read(configurationPath);
    ServiceConfiguration configuration=existingConfiguration.has_value()
        ? existingConfiguration.value()
        : ServiceConfiguration::createDefault();
    configuration.configurationFilePath=configurationPath;
    configuration.logFilePath=ConfigurationFile::defaultLogFilePath();
    // The command line wins over the file, so a user can override one setting for
    // one run without editing anything.
    if (options.warningThresholdBytes>0) {
        configuration.warningThresholdBytes=options.warningThresholdBytes;
    }
    if (options.watchdogIntervalSeconds>0) {
        configuration.checkIntervalSeconds=options.watchdogIntervalSeconds;
    }
    string validationExplanation;
    if (!configuration.isValid(validationExplanation)) {
        throw ConfigurationException(validationExplanation);
    }
    return configuration;
}
void ServiceCommand::writeInstallExplanation(const ServiceConfiguration& configuration) const {
    reporter.writeHeading("What this will do");
    reporter.writeLine(string("Register agent-db-doctor with the operating system so it starts when the ")
        + "computer does and stops cleanly when it is shut down.");
    reporter.writeLine(string("It will check the size of every supported coding agent database once an ")
        + "hour, and write one line to its log when one crosses a threshold you choose.");
    reporter.writeHeading("Where things will go");
    reporter.writeLine(configuration.describeLocations());
    reporter.writeLine(string("The configuration file is plain text and you can edit it at any time. ")
        + "Stop the service, change a number, and start it again.");
    reporter.writeHeading("The thresholds it will use");
    reporter.writeLine(string("Warns when a database passes:  ")
        + HumanReadableFormatting::formatBytes(configuration.warningThresholdBytes));
    reporter.writeLine(string("Says it is urgent past:     ")
        + HumanReadableFormatting::formatBytes(configuration.criticalThresholdBytes));
    reporter.writeLine("Checks every:                " + to_string(configuration.checkIntervalSeconds) + " seconds");
    reporter.writeWarning(
        string("Installing needs administrator rights, because the operating system keeps service "
            "files in a folder only an administrator can write to. If the command fails with a "
            "permission message, run it again with administrator rights, for example with sudo on "
            "Linux and macOS or from an elevated prompt on Windows.\n\n"
        ) + "Nothing on your machine is changed until you say yes."
    );
}
int ServiceCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (options.serviceAction.empty()) {
        reporter.writeLine("This command needs to say what to do. The choices are:");
        for (const string& action : supportedActions()) {
            reporter.writeDetailLine(action);
        }
        reporter.writeSuggestedNextSteps(vector<string>{
            string("agent-db-doctor service install"),
            string("agent-db-doctor service status")
        });
        return 1;
    }
    try {
        const ServiceConfiguration configuration=buildConfiguration(options);
        if (options.serviceAction=="status") {
            ServiceManager serviceManager(configuration);
            reporter.writeServiceStatus(serviceManager.readStatus());
            reporter.writeSuggestedNextSteps(vector<string>{
                string("agent-db-doctor service install, to start watching"),
                string("agent-db-doctor diagnose, to see the current sizes")
            });
            return 0;
        }
        if (options.serviceAction=="install") {
            writeInstallExplanation(configuration);
            if (!options.answerYesToEverything) {
                reporter.writeQuestion("Type yes to install the service, or anything else to stop: ");
                string answer;
                if (!getline(cin, answer)) {
                    reporter.writeLine("");
                    reporter.writeLine("There is nobody to answer, so nothing was installed.");
                    return 1;
                }
                if (answer!="yes") {
                    reporter.writeLine("Nothing was installed. Your machine is unchanged.");
                    return 0;
                }
            }
            ServiceManager serviceManager(configuration);
            serviceManager.install();
            reporter.writeHeading("Installed");
            reporter.writeLine(string("The service is installed and running. It will start again when the ")
                + "computer does.");
            reporter.writeLine("You can see what it has to say with: agent-db-doctor service status");
            reporter.writeLine("You can read its log at " + configuration.logFilePath);
            reporter.writeSuggestedNextSteps(vector<string>{
                string("agent-db-doctor service status, to check it is working"),
                string("Edit " + configuration.configurationFilePath + " to change the thresholds.")
            });
            return 0;
        }
        ServiceManager serviceManager(configuration);
        if (options.serviceAction=="uninstall") {
            // The state is read first, because removing something that was never
            // installed is not a removal. Saying "removed" in that case claimed a
            // change that did not happen.
            if (!serviceManager.readStatus().isInstalled) {
                reporter.writeHeading("Nothing to remove");
                reporter.writeLine(
                    "The agent-db-doctor service is not installed, so there was nothing to remove."
                );
                reporter.writeLine(
                    "Nothing was changed. If you expected it to be installed, run the service "
                    "status command to see where the tool thinks it is."
                );
                return 0;
            }
            reporter.writeLine(string("Removing the service. Your configuration file is kept, so ")
                + "reinstalling will use the same thresholds.");
            serviceManager.uninstall();
            reporter.writeHeading("Removed");
            reporter.writeLine(string("The service is no longer installed and will not start again. Nothing ")
                + "was changed about your databases.");
            return 0;
        }
        if (options.serviceAction=="start") {
            serviceManager.start();
            reporter.writeLine("The service is starting.");
            return 0;
        }
        if (options.serviceAction=="stop") {
            serviceManager.stop();
            reporter.writeLine("The service is stopping.");
            return 0;
        }
        if (options.serviceAction=="run") {
            // This is the service body. It never returns under normal operation, and
            // it prints nothing, because a service that writes to a terminal it does
            // not have is a service whose output goes nowhere useful.
            serviceManager.runWatchdogLoop();
            return 0;
        }
        reporter.writeFailure(
            string("The action ") + options.serviceAction + " is not one this command knows. "
            + "The choices are listed above.",
            string()
        );
        return 1;
    } catch (const ConfigurationException& configurationFailure) {
        reporter.writeFailure(configurationFailure.message(), string());
        reporter.writeLine("Nothing was changed.");
        return 1;
    } catch (const ServiceOperationException& serviceFailure) {
        reporter.writeFailure(serviceFailure.message(), string());
        reporter.writeLine("Nothing else was changed.");
        return 1;
    } catch (const FileSystemException& fileFailure) {
        reporter.writeFailure(fileFailure.message(), string());
        return 1;
    }
}
}
