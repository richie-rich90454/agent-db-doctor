// The implementation of the PiAgent class.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the PiAgent class declaration.
#include "AgentDbDoctor/Agents/PiAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
PiAgent::PiAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string PiAgent::displayName() const {
    return string("Pi");
}
vector<string> PiAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds CLI session
        // state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".pi"));
    }
    return candidates;
}
optional<string> PiAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> PiAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the session storage format is undocumented.
    // Returning an empty list is what makes the prune command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> PiAgent::likelyProcessNames() const {
    return vector<string>{"pi", "pi.exe"};
}
AgentDescriptor PiAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Pi keeps CLI session state under its home folder. "
        "The tool reports the size and leaves the files alone."
    );
    return descriptor;
}
}
