// The implementation of the JunieAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the JunieAgent class declaration.
#include "AgentDbDoctor/Agents/JunieAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
JunieAgent::JunieAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string JunieAgent::displayName() const {
    return string("Junie");
}
vector<string> JunieAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds agentic session
        // state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".junie"));
    }
    return candidates;
}
optional<string> JunieAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> JunieAgent::redundantEventTypes() const {
    // Junie keeps agentic session state in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> JunieAgent::likelyProcessNames() const {
    return vector<string>{"junie", "junie.exe"};
}
AgentDescriptor JunieAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Junie keeps agentic session state under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
