// The implementation of the WarpAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the WarpAgent class declaration.
#include "AgentDbDoctor/Agents/WarpAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
WarpAgent::WarpAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string WarpAgent::displayName() const {
    return string("Warp");
}
vector<string> WarpAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".warp"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/dev.warp.Warp-Stable"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".local/share/warp"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Local/Warp"));
    }
    return candidates;
}
optional<string> WarpAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> WarpAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> WarpAgent::likelyProcessNames() const {
    return vector<string>{"warp", "Warp.exe"};
}
AgentDescriptor WarpAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Warp keeps terminal session and agent state under its data dir. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
