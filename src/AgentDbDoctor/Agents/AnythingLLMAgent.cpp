// The implementation of the AnythingLLMAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AnythingLLMAgent class declaration.
#include "AgentDbDoctor/Agents/AnythingLLMAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
AnythingLLMAgent::AnythingLLMAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string AnythingLLMAgent::displayName() const {
    return string("AnythingLLM");
}
vector<string> AnythingLLMAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds chat and
        // document state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".anythingllm"));
    }
    return candidates;
}
optional<string> AnythingLLMAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> AnythingLLMAgent::redundantEventTypes() const {
    // AnythingLLM keeps chat and document state in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> AnythingLLMAgent::likelyProcessNames() const {
    return vector<string>{"anythingllm", "anythingllm.exe"};
}
AgentDescriptor AnythingLLMAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "AnythingLLM keeps local chat and document state under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
