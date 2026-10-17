// The implementation of the CopilotAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CopilotAgent class declaration.
#include "AgentDbDoctor/Agents/CopilotAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
CopilotAgent::CopilotAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string CopilotAgent::displayName() const {
    return string("GitHub Copilot");
}
vector<string> CopilotAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The chat state lives in the VS Code extension storage folder, whose
        // parent differs per platform. All three are listed so the tool works
        // on a machine set up for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/github.copilot-chat"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/github.copilot-chat"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/github.copilot-chat"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".copilot"));
    }
    return candidates;
}
optional<string> CopilotAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> CopilotAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the chat storage format is undocumented.
    // Returning an empty list is what makes the prune command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> CopilotAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code", "copilot"};
}
AgentDescriptor CopilotAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "GitHub Copilot keeps chat and session state inside the VS Code extension storage "
        "for github.copilot-chat. The tool reports the size and leaves the files alone."
    );
    return descriptor;
}
}
