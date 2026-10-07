// The implementation of the ClineAgent class.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ClineAgent class declaration.
#include "AgentDbDoctor/Agents/ClineAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
ClineAgent::ClineAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string ClineAgent::displayName() const {
    return string("Cline");
}
vector<string> ClineAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The sessions database lives under the home directory on every
        // platform, so there is only one candidate.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".cline/data/db/sessions.db"));
    }
    return candidates;
}
optional<string> ClineAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> ClineAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and Cline's tables are undocumented. Returning an
    // empty list is what makes the prune command refuse rather than guess.
    return vector<string>();
}
vector<string> ClineAgent::likelyProcessNames() const {
    return vector<string>{"cline", "cline.exe"};
}
AgentDescriptor ClineAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=true;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Cline keeps its sessions in a SQLite sessions database with undocumented tables. "
        "The tool reports its size. It does not prune it, because the tables are not "
        "documented and deleting rows on a guess could lose task state."
    );
    return descriptor;
}
}
