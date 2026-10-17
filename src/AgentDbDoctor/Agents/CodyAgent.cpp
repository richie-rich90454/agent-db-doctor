// The implementation of the CodyAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CodyAgent class declaration.
#include "AgentDbDoctor/Agents/CodyAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
CodyAgent::CodyAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string CodyAgent::displayName() const {
    return string("Cody");
}
vector<string> CodyAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The extension storage parent differs per platform, so all three are
        // listed alongside the home folder. All are listed so the tool works
        // on a machine set up for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".cody"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/sourcegraph.cody-ai"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/sourcegraph.cody-ai"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/sourcegraph.cody-ai"));
    }
    return candidates;
}
optional<string> CodyAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> CodyAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the chat storage format is undocumented.
    // Returning an empty list is what makes the prune command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> CodyAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor CodyAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Cody keeps chat state in its home folder plus editor extension storage. "
        "The tool reports the size and leaves the files alone."
    );
    return descriptor;
}
}
