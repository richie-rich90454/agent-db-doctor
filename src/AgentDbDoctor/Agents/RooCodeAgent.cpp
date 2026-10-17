// The implementation of the RooCodeAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the RooCodeAgent class declaration.
#include "AgentDbDoctor/Agents/RooCodeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
RooCodeAgent::RooCodeAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string RooCodeAgent::displayName() const {
    return string("Roo Code");
}
vector<string> RooCodeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The task folder lives directly under the home directory on every
        // platform, so it comes first and needs no platform check.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".roo"));
        // Extension storage follows the VS Code convention, which differs per
        // platform. All three are listed so the tool works on a machine set up
        // for a different platform. Only Roo Code's own extension id is listed:
        // the older id under `saoudrizwan.claude-dev` belongs to the Cline
        // extension, which has its own adapter, and claiming it here made Cline's
        // folder appear in reports as Roo Code.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/rooveterinaryinc.roo-cline"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/rooveterinaryinc.roo-cline"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/rooveterinaryinc.roo-cline"));
    }
    return candidates;
}
optional<string> RooCodeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> RooCodeAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and Roo Code's storage is undocumented. Returning
    // an empty list is what makes the prune command refuse rather than guess.
    return vector<string>();
}
vector<string> RooCodeAgent::likelyProcessNames() const {
    return vector<string>{"Roo Code", "Code.exe", "code"};
}
AgentDescriptor RooCodeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Roo Code keeps task state in its own folder plus VS Code extension storage. "
        "The tool reports the folders and never changes them."
    );
    return descriptor;
}
}
