// The implementation of the SourceryAgent class.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the SourceryAgent class declaration.
#include "AgentDbDoctor/Agents/SourceryAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
SourceryAgent::SourceryAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string SourceryAgent::displayName() const {
    return string("Sourcery");
}
vector<string> SourceryAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The extension storage parent differs per platform, so all three are
        // listed. All are listed so the tool works on a machine set up for
        // a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/sourcery.sourcery"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/sourcery.sourcery"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/sourcery.sourcery"));
    }
    return candidates;
}
optional<string> SourceryAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> SourceryAgent::redundantEventTypes() const {
    // Sourcery keeps review state in editor extension storage and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> SourceryAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor SourceryAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Sourcery keeps review state in editor extension storage. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
