// The implementation of the CodexAgent class.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CodexAgent class declaration.
#include "AgentDbDoctor/Agents/CodexAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
CodexAgent::CodexAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string CodexAgent::displayName() const {
    return string("Codex CLI");
}
vector<string> CodexAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // The CLI is written in Rust and follows the same convention on Windows as
        // on Unix, which is why there is only one candidate there.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".codex/sessions"));
    }
    // An explicit override lets a developer point the tool at a test folder
    // without touching their real sessions.
    string overridePath;
    if (PlatformMetadata::readEnvironmentVariable("CODEX_HOME", overridePath)) {
        candidates.push_back(AgentRegistry::joinPath(overridePath, "sessions"));
    }
    return candidates;
}
optional<string> CodexAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> CodexAgent::redundantEventTypes() const {
    // The CLI stores one JSON object per line and never writes a second version of
    // a message, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> CodexAgent::likelyProcessNames() const {
    return vector<string>{"codex", "codex.exe"};
}
AgentDescriptor CodexAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "The Codex CLI writes one text file per session, with one JSON object on every line. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
