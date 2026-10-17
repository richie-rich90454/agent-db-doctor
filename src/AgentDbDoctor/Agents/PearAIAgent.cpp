// The implementation of the PearAIAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the PearAIAgent class declaration.
#include "AgentDbDoctor/Agents/PearAIAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
PearAIAgent::PearAIAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string PearAIAgent::displayName() const {
    return string("PearAI");
}
vector<string> PearAIAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".pearai"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/PearAI"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/PearAI"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/PearAI"));
    }
    return candidates;
}
optional<string> PearAIAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> PearAIAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> PearAIAgent::likelyProcessNames() const {
    return vector<string>{"PearAI", "PearAI.exe"};
}
AgentDescriptor PearAIAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "PearAI is a VS Code fork; it keeps session and workspace state under its data dir. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
