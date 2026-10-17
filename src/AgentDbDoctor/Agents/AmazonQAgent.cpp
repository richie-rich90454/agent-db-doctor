// The implementation of the AmazonQAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AmazonQAgent class declaration.
#include "AgentDbDoctor/Agents/AmazonQAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
AmazonQAgent::AmazonQAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string AmazonQAgent::displayName() const {
    return string("Amazon Q");
}
vector<string> AmazonQAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds CLI session state
        // rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".amazonq"));
    }
    return candidates;
}
optional<string> AmazonQAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> AmazonQAgent::redundantEventTypes() const {
    // Amazon Q keeps CLI session state under its home folder and never writes
    // a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> AmazonQAgent::likelyProcessNames() const {
    return vector<string>{"q", "amazonq", "q.exe"};
}
AgentDescriptor AmazonQAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Amazon Q keeps CLI session state under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
