// The implementation of the LlmAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the LlmAgent class declaration.
#include "AgentDbDoctor/Agents/LlmAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
LlmAgent::LlmAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string LlmAgent::displayName() const {
    return string("LLM");
}
vector<string> LlmAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds logged prompts
        // and responses rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".llm"));
    }
    return candidates;
}
optional<string> LlmAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> LlmAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> LlmAgent::likelyProcessNames() const {
    return vector<string>{"llm", "llm.exe"};
}
AgentDescriptor LlmAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "LLM keeps logged prompts and responses under its home folder. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
