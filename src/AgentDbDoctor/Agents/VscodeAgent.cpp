// The implementation of the VscodeAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the VscodeAgent class declaration.
#include "AgentDbDoctor/Agents/VscodeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
// The name of the SQLite file VS Code writes inside each workspace folder.
static constexpr const char* WORKSPACE_STATE_FILE="state.vscdb";
VscodeAgent::VscodeAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string VscodeAgent::displayName() const {
    return string("VS Code");
}
vector<string> VscodeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    // VS Code is an editor, so it keeps its data in the platform's application
    // support folder rather than in a dot folder in the home directory. The three
    // platforms each have a different convention, and all three are listed so
    // that the tool works on a machine set up for a different platform.
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "Library/Application Support/Code/User/workspaceStorage"
        ));
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            ".config/Code/User/workspaceStorage"
        ));
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "AppData/Roaming/Code/User/workspaceStorage"
        ));
    }
    return candidates;
}
optional<string> VscodeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> VscodeAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the editor state tables are undocumented.
    // Returning an empty list is what makes the compact command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> VscodeAgent::likelyProcessNames() const {
    return vector<string>{"Code", "Code.exe", "code"};
}
vector<DiscoveredDatabase> VscodeAgent::findWorkspaceDatabases(int maximumWorkspaceCount) const {
    vector<DiscoveredDatabase> foundDatabases;
    const optional<string> storageFolder=databaseLocator.findFirstExistingPath();
    if (!storageFolder.has_value()) {
        return foundDatabases;
    }
    // Each entry in the storage folder is one workspace, named by a hash. The
    // listing is one level deep and bounded, so this is a fixed number of system
    // calls rather than a walk of the whole tree.
    const vector<string> workspaceNames=AgentRegistry::directoryEntryNames(
        storageFolder.value(),
        maximumWorkspaceCount
    );
    for (const string& workspaceName : workspaceNames) {
        const string workspacePath=AgentRegistry::joinPath(storageFolder.value(), workspaceName);
        const string stateFilePath=AgentRegistry::joinPath(workspacePath, WORKSPACE_STATE_FILE);
        const FileMetadata stateFileStatus=PlatformMetadata::readFileMetadata(stateFilePath);
        if (!stateFileStatus.exists || stateFileStatus.isDirectory) {
            continue;
        }
        DiscoveredDatabase discovered;
        discovered.agentName=displayName();
        discovered.filePath=stateFilePath;
        discovered.isSqliteDatabase=true;
        discovered.sizeBytes=stateFileStatus.logicalSizeBytes;
        foundDatabases.push_back(discovered);
        if (static_cast<int>(foundDatabases.size())>=maximumWorkspaceCount) {
            break;
        }
    }
    return foundDatabases;
}
AgentDescriptor VscodeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=true;
    // Compaction is not offered. The tables are undocumented, so deleting anything
    // from them would be guessing, and the specification forbids that.
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "VS Code keeps one state.vscdb per workspace, in a folder named after a hash of the "
        "workspace path. The tool reports their sizes. It does not compact them, because they hold "
        "editor state in undocumented tables and deleting rows on a guess could lose it. The "
        "vacuum command is always available for them: it hands unused space back to the drive "
        "and changes nothing the databases hold."
    );
    return descriptor;
}
}
