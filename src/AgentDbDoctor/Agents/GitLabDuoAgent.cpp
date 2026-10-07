// The implementation of the GitLabDuoAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the GitLabDuoAgent class declaration.
#include "AgentDbDoctor/Agents/GitLabDuoAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
GitLabDuoAgent::GitLabDuoAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string GitLabDuoAgent::displayName() const {
    return string("GitLab Duo");
}
vector<string> GitLabDuoAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // GitLab Duo lives in editor extension storage, whose parent moves
        // per platform, so every known location is checked in order.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/gitlab.gitlab-workflow"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/gitlab.gitlab-workflow"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/gitlab.gitlab-workflow"));
    }
    return candidates;
}
optional<string> GitLabDuoAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> GitLabDuoAgent::redundantEventTypes() const {
    // GitLab Duo keeps assistant state in editor extension storage and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> GitLabDuoAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor GitLabDuoAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "GitLab Duo keeps assistant state in editor extension storage. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
