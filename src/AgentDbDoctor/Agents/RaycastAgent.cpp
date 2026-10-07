// The implementation of the RaycastAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the RaycastAgent class declaration.
#include "AgentDbDoctor/Agents/RaycastAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
RaycastAgent::RaycastAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string RaycastAgent::displayName() const {
    return string("Raycast");
}
vector<string> RaycastAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/com.raycast.macos"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/raycast"));
    }
    return candidates;
}
optional<string> RaycastAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> RaycastAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> RaycastAgent::likelyProcessNames() const {
    return vector<string>{"Raycast", "Raycast.exe"};
}
AgentDescriptor RaycastAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Raycast keeps launcher and AI extension state under its data dir. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
