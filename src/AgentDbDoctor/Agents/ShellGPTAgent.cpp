// The implementation of the ShellGPTAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ShellGPTAgent class declaration.
#include "AgentDbDoctor/Agents/ShellGPTAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
ShellGPTAgent::ShellGPTAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string ShellGPTAgent::displayName() const {
    return string("ShellGPT");
}
vector<string> ShellGPTAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds terminal session
        // state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/shell_gpt"));
    }
    return candidates;
}
optional<string> ShellGPTAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> ShellGPTAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> ShellGPTAgent::likelyProcessNames() const {
    return vector<string>{"sgpt", "sgpt.exe"};
}
AgentDescriptor ShellGPTAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "ShellGPT keeps terminal session state under its config folder. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
