// The implementation of the SupermavenAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the SupermavenAgent class declaration.
#include "AgentDbDoctor/Agents/SupermavenAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
SupermavenAgent::SupermavenAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string SupermavenAgent::displayName() const {
    return string("Supermaven");
}
vector<string> SupermavenAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The home folder holds completion state on every platform, so it
        // comes first and needs no platform check.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".supermaven"));
        // Extension storage follows the VS Code convention, which differs per
        // platform. All three are listed so the tool works on a machine set up
        // for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/supermaven.supermaven"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/supermaven.supermaven"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/supermaven.supermaven"));
    }
    return candidates;
}
optional<string> SupermavenAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> SupermavenAgent::redundantEventTypes() const {
    // Supermaven keeps completion state in editor extension storage and never
    // writes a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> SupermavenAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor SupermavenAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Supermaven keeps completion state in editor extension storage. "
        "The tool reports the folders and never changes them."
    );
    return descriptor;
}
}
