// The implementation of the LMStudioAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the LMStudioAgent class declaration.
#include "AgentDbDoctor/Agents/LMStudioAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
LMStudioAgent::LMStudioAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string LMStudioAgent::displayName() const {
    return string("LM Studio");
}
vector<string> LMStudioAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds chat history
        // rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".lmstudio"));
    }
    return candidates;
}
optional<string> LMStudioAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> LMStudioAgent::redundantEventTypes() const {
    // LM Studio keeps chat history in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> LMStudioAgent::likelyProcessNames() const {
    return vector<string>{"lmstudio", "lmstudio.exe"};
}
AgentDescriptor LMStudioAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "LM Studio keeps local chat history under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
