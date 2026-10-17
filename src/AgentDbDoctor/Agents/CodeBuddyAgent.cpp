// The implementation of the CodeBuddyAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CodeBuddyAgent class declaration.
#include "AgentDbDoctor/Agents/CodeBuddyAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
CodeBuddyAgent::CodeBuddyAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string CodeBuddyAgent::displayName() const {
    return string("CodeBuddy");
}
vector<string> CodeBuddyAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds IDE and CLI task
        // state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".codebuddy"));
    }
    return candidates;
}
optional<string> CodeBuddyAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> CodeBuddyAgent::redundantEventTypes() const {
    // CodeBuddy keeps IDE and CLI task state in a home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> CodeBuddyAgent::likelyProcessNames() const {
    return vector<string>{"codebuddy", "codebuddy.exe"};
}
AgentDescriptor CodeBuddyAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "CodeBuddy is a Tencent IDE and CLI agent that keeps task state in a home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
