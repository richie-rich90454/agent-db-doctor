// The implementation of the ComateAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ComateAgent class declaration.
#include "AgentDbDoctor/Agents/ComateAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
ComateAgent::ComateAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string ComateAgent::displayName() const {
    return string("Comate");
}
vector<string> ComateAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds assistant session
        // state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".comate"));
    }
    return candidates;
}
optional<string> ComateAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> ComateAgent::redundantEventTypes() const {
    // Comate keeps assistant session state in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> ComateAgent::likelyProcessNames() const {
    return vector<string>{"comate", "comate.exe"};
}
AgentDescriptor ComateAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Comate keeps assistant session state under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
