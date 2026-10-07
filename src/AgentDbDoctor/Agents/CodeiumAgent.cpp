// The implementation of the CodeiumAgent class.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CodeiumAgent class declaration.
#include "AgentDbDoctor/Agents/CodeiumAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
CodeiumAgent::CodeiumAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string CodeiumAgent::displayName() const {
    return string("Codeium");
}
vector<string> CodeiumAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The extension storage parent differs per platform, so all three are
        // listed alongside the home folder. All are listed so the tool works
        // on a machine set up for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".codeium"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/codeium.codeium"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/codeium.codeium"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/codeium.codeium"));
    }
    return candidates;
}
optional<string> CodeiumAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> CodeiumAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the assistant storage format is undocumented.
    // Returning an empty list is what makes the prune command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> CodeiumAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor CodeiumAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Codeium keeps assistant state in its home folder plus editor extension storage. "
        "The tool reports the size and leaves the files alone."
    );
    return descriptor;
}
}
