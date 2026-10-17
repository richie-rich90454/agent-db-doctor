// The implementation of the AmpAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AmpAgent class declaration.
#include "AgentDbDoctor/Agents/AmpAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
AmpAgent::AmpAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string AmpAgent::displayName() const {
    return string("Amp");
}
vector<string> AmpAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds CLI session state
        // rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".amp"));
    }
    return candidates;
}
optional<string> AmpAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> AmpAgent::redundantEventTypes() const {
    // Diagnose-only: this tool does not reverse engineer undocumented schemas/storage,
    // so nothing here can be identified as redundant.
    return vector<string>();
}
vector<string> AmpAgent::likelyProcessNames() const {
    return vector<string>{"amp", "amp.exe"};
}
AgentDescriptor AmpAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Amp keeps CLI session state under its home folder. "
        "The layout is reported, never changed."
    );
    return descriptor;
}
}
