// The implementation of the IntelliCodeAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the IntelliCodeAgent class declaration.
#include "AgentDbDoctor/Agents/IntelliCodeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
IntelliCodeAgent::IntelliCodeAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string IntelliCodeAgent::displayName() const {
    return string("IntelliCode");
}
vector<string> IntelliCodeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The extension storage lives under the editor profile on every platform,
        // so there is one candidate per platform layout.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/visualstudioexptteam.vscodeintellicode"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/visualstudioexptteam.vscodeintellicode"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/visualstudioexptteam.vscodeintellicode"));
    }
    return candidates;
}
optional<string> IntelliCodeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> IntelliCodeAgent::redundantEventTypes() const {
    // IntelliCode keeps completion models and state in editor extension storage
    // and never writes a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> IntelliCodeAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor IntelliCodeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "IntelliCode keeps completion models and state in editor extension storage. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
