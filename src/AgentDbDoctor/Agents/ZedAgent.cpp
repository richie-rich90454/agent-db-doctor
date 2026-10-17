// The implementation of the ZedAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ZedAgent class declaration.
#include "AgentDbDoctor/Agents/ZedAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
ZedAgent::ZedAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string ZedAgent::displayName() const {
    return string("Zed");
}
vector<string> ZedAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Zed is a native editor, so it keeps its data in the platform's
        // application support folder. All three platforms are listed so that
        // the tool works on a machine set up for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Zed"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".local/share/zed"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Local/Zed"));
    }
    return candidates;
}
optional<string> ZedAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> ZedAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and Zed's tables are undocumented. Returning an
    // empty list is what makes the prune command refuse rather than guess.
    return vector<string>();
}
vector<string> ZedAgent::likelyProcessNames() const {
    return vector<string>{"zed", "zed.exe"};
}
AgentDescriptor ZedAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=true;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Zed keeps its database and extensions under the platform user-data dir. "
        "The tool reports their sizes. It never prunes them, because the schema is undocumented."
    );
    return descriptor;
}
}
