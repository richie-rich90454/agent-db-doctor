// The implementation of the LingmaAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the LingmaAgent class declaration.
#include "AgentDbDoctor/Agents/LingmaAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
LingmaAgent::LingmaAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string LingmaAgent::displayName() const {
    return string("Lingma");
}
vector<string> LingmaAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds assistant session
        // state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".lingma"));
    }
    return candidates;
}
optional<string> LingmaAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> LingmaAgent::redundantEventTypes() const {
    // Lingma keeps assistant session state in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> LingmaAgent::likelyProcessNames() const {
    return vector<string>{"lingma", "lingma.exe"};
}
AgentDescriptor LingmaAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Lingma keeps assistant session state under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
