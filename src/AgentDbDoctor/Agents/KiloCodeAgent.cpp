// The implementation of the KiloCodeAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the KiloCodeAgent class declaration.
#include "AgentDbDoctor/Agents/KiloCodeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
KiloCodeAgent::KiloCodeAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string KiloCodeAgent::displayName() const {
    return string("Kilo Code");
}
vector<string> KiloCodeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".kilo"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/kilocode.kilo-code"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/kilocode.kilo-code"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/kilocode.kilo-code"));
    }
    return candidates;
}
optional<string> KiloCodeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> KiloCodeAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> KiloCodeAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor KiloCodeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Kilo Code keeps task state in its own folder plus VS Code extension storage. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
