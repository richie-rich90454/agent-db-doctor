// The implementation of the QoderAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the QoderAgent class declaration.
#include "AgentDbDoctor/Agents/QoderAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
QoderAgent::QoderAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string QoderAgent::displayName() const {
    return string("Qoder");
}
vector<string> QoderAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Qoder uses a dot folder on Linux and platform data directories
        // elsewhere, so every known location is checked in order.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".qoder"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Qoder"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Qoder"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Qoder"));
    }
    return candidates;
}
optional<string> QoderAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> QoderAgent::redundantEventTypes() const {
    // Qoder keeps session and workspace state in a data directory and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> QoderAgent::likelyProcessNames() const {
    return vector<string>{"Qoder", "Qoder.exe"};
}
AgentDescriptor QoderAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Qoder keeps session and workspace state under its data dir. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
