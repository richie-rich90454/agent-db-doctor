// The implementation of the KimiAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the KimiAgent class declaration.
#include "AgentDbDoctor/Agents/KimiAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
KimiAgent::KimiAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string KimiAgent::displayName() const {
    return string("Kimi");
}
vector<string> KimiAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented data root on every platform is under the home directory.
        // There is a single default candidate, because the CLI follows the same
        // convention on Windows as on Unix.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".kimi"));
    }
    // An explicit override lets a developer point the tool at a shared folder
    // without touching their real session data.
    string overridePath;
    if (PlatformMetadata::readEnvironmentVariable("KIMI_SHARE_DIR", overridePath)) {
        candidates.push_back(overridePath);
    }
    return candidates;
}
optional<string> KimiAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> KimiAgent::redundantEventTypes() const {
    // Kimi keeps session data in a data root and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> KimiAgent::likelyProcessNames() const {
    return vector<string>{"kimi", "kimi.exe"};
}
AgentDescriptor KimiAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Kimi keeps session data under its data root with per-work-directory sessions. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
