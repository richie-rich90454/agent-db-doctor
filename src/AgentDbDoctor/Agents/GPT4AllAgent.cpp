// The implementation of the GPT4AllAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the GPT4AllAgent class declaration.
#include "AgentDbDoctor/Agents/GPT4AllAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
GPT4AllAgent::GPT4AllAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string GPT4AllAgent::displayName() const {
    return string("GPT4All");
}
vector<string> GPT4AllAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented locations differ per platform, so every known data dir
        // is checked in order and the first one that exists wins.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/GPT4All"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/GPT4All"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Local/GPT4All"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".gpt4all"));
    }
    return candidates;
}
optional<string> GPT4AllAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> GPT4AllAgent::redundantEventTypes() const {
    // GPT4All keeps chat history in a data directory and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> GPT4AllAgent::likelyProcessNames() const {
    return vector<string>{"gpt4all", "gpt4all.exe"};
}
AgentDescriptor GPT4AllAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "GPT4All keeps local chat history under its data dir. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
