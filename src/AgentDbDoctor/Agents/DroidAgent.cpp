// The implementation of the DroidAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DroidAgent class declaration.
#include "AgentDbDoctor/Agents/DroidAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
DroidAgent::DroidAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string DroidAgent::displayName() const {
    return string("Droid");
}
vector<string> DroidAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Droid keeps terminal session state under two home folders, so both
        // are checked and the first one that exists wins.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".factory"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".droid"));
    }
    return candidates;
}
optional<string> DroidAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> DroidAgent::redundantEventTypes() const {
    // Droid keeps terminal session state in home folders and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> DroidAgent::likelyProcessNames() const {
    return vector<string>{"droid", "droid.exe"};
}
AgentDescriptor DroidAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Droid keeps terminal session state under its home folders. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
