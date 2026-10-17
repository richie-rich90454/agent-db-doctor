// The implementation of the TabbyAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the TabbyAgent class declaration.
#include "AgentDbDoctor/Agents/TabbyAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
TabbyAgent::TabbyAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string TabbyAgent::displayName() const {
    return string("Tabby");
}
vector<string> TabbyAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The home folder holds the self-hosted state, and the remaining entries
        // cover the editor extension storage on each platform layout.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".tabby"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/tabbyml.vscode-tabby"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/tabbyml.vscode-tabby"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/tabbyml.vscode-tabby"));
    }
    return candidates;
}
optional<string> TabbyAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> TabbyAgent::redundantEventTypes() const {
    // Tabby keeps assistant state in its home folder and editor extension storage
    // and never writes a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> TabbyAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor TabbyAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Tabby keeps self-hosted assistant state in its home folder plus editor extension storage. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
