// The implementation of the GptmeAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the GptmeAgent class declaration.
#include "AgentDbDoctor/Agents/GptmeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
GptmeAgent::GptmeAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string GptmeAgent::displayName() const {
    return string("gptme");
}
vector<string> GptmeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds conversation
        // logs rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/gptme"));
    }
    return candidates;
}
optional<string> GptmeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> GptmeAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> GptmeAgent::likelyProcessNames() const {
    return vector<string>{"gptme", "gptme.exe"};
}
AgentDescriptor GptmeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "gptme keeps conversation logs under its config folder. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
