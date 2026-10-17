// The implementation of the TraeAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the TraeAgent class declaration.
#include "AgentDbDoctor/Agents/TraeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
TraeAgent::TraeAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string TraeAgent::displayName() const {
    return string("Trae");
}
vector<string> TraeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".trae"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Trae"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Trae"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Trae"));
    }
    return candidates;
}
optional<string> TraeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> TraeAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> TraeAgent::likelyProcessNames() const {
    return vector<string>{"Trae", "Trae.exe"};
}
AgentDescriptor TraeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Trae is a VS Code fork; it keeps session and workspace state under its data dir. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
