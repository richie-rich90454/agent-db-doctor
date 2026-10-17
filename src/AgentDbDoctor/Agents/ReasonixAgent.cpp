// The implementation of the ReasonixAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ReasonixAgent class declaration.
#include "AgentDbDoctor/Agents/ReasonixAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
ReasonixAgent::ReasonixAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string ReasonixAgent::displayName() const {
    return string("Reasonix");
}
vector<string> ReasonixAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds skills and
        // configuration rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".reasonix"));
    }
    return candidates;
}
optional<string> ReasonixAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> ReasonixAgent::redundantEventTypes() const {
    // Reasonix keeps skills and configuration in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> ReasonixAgent::likelyProcessNames() const {
    return vector<string>{"reasonix", "reasonix.exe"};
}
AgentDescriptor ReasonixAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Reasonix keeps skills and configuration in a home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
