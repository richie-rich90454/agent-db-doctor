// The implementation of the BitoAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the BitoAgent class declaration.
#include "AgentDbDoctor/Agents/BitoAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
BitoAgent::BitoAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string BitoAgent::displayName() const {
    return string("Bito");
}
vector<string> BitoAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Extension storage follows the VS Code convention, which differs per
        // platform. All three are listed so the tool works on a machine set up
        // for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/BitoExtension.bito"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/BitoExtension.bito"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/BitoExtension.bito"));
    }
    return candidates;
}
optional<string> BitoAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> BitoAgent::redundantEventTypes() const {
    // Bito keeps assistant state in editor extension storage and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> BitoAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor BitoAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Bito keeps assistant state in editor extension storage. "
        "The tool reports the folders and never changes them."
    );
    return descriptor;
}
}
