// The implementation of the WorkbuddyAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the WorkbuddyAgent class declaration.
#include "AgentDbDoctor/Agents/WorkbuddyAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
WorkbuddyAgent::WorkbuddyAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string WorkbuddyAgent::displayName() const {
    return string("WorkBuddy");
}
vector<string> WorkbuddyAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds task state
        // rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".workbuddy"));
    }
    return candidates;
}
optional<string> WorkbuddyAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> WorkbuddyAgent::redundantEventTypes() const {
    // WorkBuddy keeps task state in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> WorkbuddyAgent::likelyProcessNames() const {
    return vector<string>{"workbuddy", "workbuddy.exe"};
}
AgentDescriptor WorkbuddyAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "WorkBuddy is a Tencent desktop agent workstation that keeps task state in a home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
