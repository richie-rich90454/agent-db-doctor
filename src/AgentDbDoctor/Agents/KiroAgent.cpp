// The implementation of the KiroAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the KiroAgent class declaration.
#include "AgentDbDoctor/Agents/KiroAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
KiroAgent::KiroAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string KiroAgent::displayName() const {
    return string("Kiro");
}
vector<string> KiroAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented global scope on every platform is under the home directory.
        // There is a single default candidate, because the global directory holds
        // agents, skills, steering, and sessions rather than platform data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".kiro"));
    }
    // An explicit override lets a developer point the tool at a test folder
    // without touching their real global directory.
    string overridePath;
    if (PlatformMetadata::readEnvironmentVariable("KIRO_HOME", overridePath)) {
        candidates.push_back(overridePath);
    }
    return candidates;
}
optional<string> KiroAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> KiroAgent::redundantEventTypes() const {
    // Kiro keeps agents, skills, steering, and sessions in a global directory
    // and never writes a session database, so there is nothing redundant.
    return vector<string>();
}
vector<string> KiroAgent::likelyProcessNames() const {
    return vector<string>{"kiro", "kiro.exe"};
}
AgentDescriptor KiroAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Kiro keeps agents, skills, steering, and sessions under its global directory. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
