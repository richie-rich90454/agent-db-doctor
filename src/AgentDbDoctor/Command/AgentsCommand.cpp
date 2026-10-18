// The implementation of the AgentsCommand class.
//
// The listing has three levels of detail, and each one answers a different
// question. By default an agent with data on this machine is printed with the path
// it was found at. `--verbose` adds the candidate paths for every agent, which is
// where the tool looked. `--machine-readable` turns the same information into one
// JSON object per agent, which is what the graphical interface and any script read.
// Provides `tolower`, used for the case insensitive name comparison.
#include <cctype>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AgentsCommand class declaration.
#include "AgentDbDoctor/Command/AgentsCommand.hpp"
// Provides the registry that knows every supported agent and finds its data.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
AgentsCommand::AgentsCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string AgentsCommand::describe() const {
    return string(
        "List every supported coding agent and show where its data was found on this machine. "
        "With --verbose every candidate path that was looked at is shown too, which is also the "
        "map of where each agent keeps its data."
    );
}
string AgentsCommand::commandName() const {
    return string("agents");
}
int AgentsCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    const AgentRegistry registry;
    const vector<AgentDescriptor> allAgents=registry.allAgents();
    const vector<DiscoveredDatabase> discoveredDatabases=registry.discoverAllDatabases();
    // The candidate paths are shown by `--verify`, or by `--verbose` for anyone who
    // thinks of it that way. Both mean "show me where you looked".
    const bool showCandidatePaths=options.verifyAgentPaths || options.verbose;
    // The requested name is compared without regard to capitalisation, the same rule
    // the registry uses, so `opencode` and `OpenCode` both work.
    string requestedName=options.agentName;
    for (char& currentCharacter : requestedName) {
        currentCharacter=static_cast<char>(tolower(static_cast<unsigned char>(currentCharacter)));
    }
    int agentsWithDataCount=0;
    int listedAgentCount=0;
    string heading=string("Supported agents (") + to_string(allAgents.size()) + " in total)";
    reporter.writeHeading(heading);
    for (const AgentDescriptor& descriptor : allAgents) {
        if (!requestedName.empty()) {
            string candidateName=descriptor.displayName;
            for (char& currentCharacter : candidateName) {
                currentCharacter=static_cast<char>(tolower(static_cast<unsigned char>(currentCharacter)));
            }
            if (candidateName!=requestedName) {
                continue;
            }
        }
        listedAgentCount++;
        vector<string> foundPathsForThisAgent;
        for (const DiscoveredDatabase& discovered : discoveredDatabases) {
            if (discovered.agentName==descriptor.displayName) {
                foundPathsForThisAgent.push_back(discovered.filePath);
            }
        }
        if (!foundPathsForThisAgent.empty()) {
            agentsWithDataCount++;
        }
        if (reporter.isMachineReadable()) {
            string jsonText="{\"event\":\"agent\"";
            jsonText+=",\"name\":\"" + ConsoleReport::escapeForJson(descriptor.displayName) + "\"";
            jsonText+=",\"storage\":\"" + ConsoleReport::escapeForJson(descriptor.storageDescription) + "\"";
            jsonText+=",\"supportsCompaction\":" + string(descriptor.supportsCompaction ? "true" : "false");
            jsonText+=",\"foundPaths\":[";
            for (size_t pathIndex=0; pathIndex<foundPathsForThisAgent.size(); ++pathIndex) {
                if (pathIndex>0) {
                    jsonText+=",";
                }
                jsonText+="\"" + ConsoleReport::escapeForJson(foundPathsForThisAgent[pathIndex]) + "\"";
            }
            jsonText+="]";
            if (showCandidatePaths) {
                jsonText+=",\"candidatePaths\":[";
                for (size_t pathIndex=0; pathIndex<descriptor.candidatePaths.size(); ++pathIndex) {
                    if (pathIndex>0) {
                        jsonText+=",";
                    }
                    jsonText+="\"" + ConsoleReport::escapeForJson(descriptor.candidatePaths[pathIndex]) + "\"";
                }
                jsonText+="]";
            }
            jsonText+="}";
            reporter.writeMachineObject(jsonText);
            continue;
        }
        reporter.writeLine(descriptor.displayName);
        reporter.writeDetailLine(descriptor.storageDescription);
        if (foundPathsForThisAgent.empty()) {
            reporter.writeDetailLine("Nothing found on this machine.");
            if (showCandidatePaths) {
                reporter.writeDetailLine("Looked in:");
                for (const string& candidatePath : descriptor.candidatePaths) {
                    reporter.writeDetailLine("    " + candidatePath);
                }
            }
        } else if (foundPathsForThisAgent.size()==1) {
            reporter.writeDetailLine("Found: " + foundPathsForThisAgent.front());
        } else {
            reporter.writeDetailLine(
                string("Found ") + to_string(foundPathsForThisAgent.size()) + ":"
            );
            for (const string& foundPath : foundPathsForThisAgent) {
                reporter.writeDetailLine("    " + foundPath);
            }
        }
    }
    if (!reporter.isMachineReadable()) {
        reporter.writeLine("");
        reporter.writeLine(
            string("Listed ") + to_string(listedAgentCount) + " of " + to_string(allAgents.size())
            + " agents; " + to_string(agentsWithDataCount) + " have data on this machine."
        );
        if (!options.verbose && agentsWithDataCount<listedAgentCount) {
            reporter.writeSuggestedNextSteps(vector<string>{
                string("Run this command with --verbose to see every path that was looked at, "
                    "including the candidates that were not found.")
            });
        }
    } else {
        reporter.writeMachineResult("agents", vector<MachineField>{
            MachineField::number("agentsListed", listedAgentCount),
            MachineField::number("agentsWithData", agentsWithDataCount)
        });
    }
    return 0;
}
}
