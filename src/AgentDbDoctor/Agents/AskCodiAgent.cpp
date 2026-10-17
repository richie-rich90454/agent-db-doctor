// The implementation of the AskCodiAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AskCodiAgent class declaration.
#include "AgentDbDoctor/Agents/AskCodiAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
AskCodiAgent::AskCodiAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string AskCodiAgent::displayName() const {
    return string("AskCodi");
}
vector<string> AskCodiAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Extension storage follows the VS Code convention, which differs per
        // platform. All three are listed so the tool works on a machine set up
        // for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/askcodi.askcodi"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/askcodi.askcodi"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/askcodi.askcodi"));
    }
    return candidates;
}
optional<string> AskCodiAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> AskCodiAgent::redundantEventTypes() const {
    // AskCodi keeps assistant state in editor extension storage and never
    // writes a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> AskCodiAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor AskCodiAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "AskCodi keeps assistant state in editor extension storage. "
        "The tool reports the folders and never changes them."
    );
    return descriptor;
}
}
