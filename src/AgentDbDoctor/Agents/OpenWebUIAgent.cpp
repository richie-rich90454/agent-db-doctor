// The implementation of the OpenWebUIAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the OpenWebUIAgent class declaration.
#include "AgentDbDoctor/Agents/OpenWebUIAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
OpenWebUIAgent::OpenWebUIAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string OpenWebUIAgent::displayName() const {
    return string("Open WebUI");
}
vector<string> OpenWebUIAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds chat history
        // rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".open-webui"));
    }
    return candidates;
}
optional<string> OpenWebUIAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> OpenWebUIAgent::redundantEventTypes() const {
    // Open WebUI keeps chat history in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> OpenWebUIAgent::likelyProcessNames() const {
    return vector<string>{"open-webui", "open-webui.exe"};
}
AgentDescriptor OpenWebUIAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Open WebUI keeps self-hosted chat history under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
