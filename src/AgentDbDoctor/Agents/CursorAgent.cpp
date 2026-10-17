// The implementation of the CursorAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CursorAgent class declaration.
#include "AgentDbDoctor/Agents/CursorAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
// The name of the SQLite file Cursor writes inside each workspace folder.
static constexpr const char* WORKSPACE_STATE_FILE="state.vscdb";
CursorAgent::CursorAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string CursorAgent::displayName() const {
    return string("Cursor");
}
vector<string> CursorAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    // Cursor is an editor, so it keeps its data in the platform's application
    // support folder rather than in a dot folder in the home directory. The three
    // platforms each have a different convention, and all three are listed so
    // that the tool works on a machine set up for a different platform.
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "Library/Application Support/Cursor/User/workspaceStorage"
        ));
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            ".config/Cursor/User/workspaceStorage"
        ));
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "AppData/Roaming/Cursor/User/workspaceStorage"
        ));
    }
#if defined(__APPLE__)
    // On macOS the same folder is reachable through the system wide location too,
    // which matters when the tool runs as a launch daemon with a different home.
    candidates.push_back(string(
        "/Users/Shared/Library/Application Support/Cursor/User/workspaceStorage"
    ));
#endif
    return candidates;
}
optional<string> CursorAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> CursorAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and Cursor's tables are undocumented. Returning an
    // empty list is what makes the compact command refuse rather than guess.
    return vector<string>();
}
vector<string> CursorAgent::likelyProcessNames() const {
    return vector<string>{"Cursor", "Cursor.exe"};
}
vector<DiscoveredDatabase> CursorAgent::findWorkspaceDatabases(int maximumWorkspaceCount) const {
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
AgentDescriptor CursorAgent::describe() const {
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
        "Cursor keeps one SQLite database per workspace, in a folder named after a hash of the "
        "workspace path. The tool reports their sizes. It does not compact them, because their tables "
        "are not documented and deleting rows on a guess could lose editor state. The vacuum "
        "command is always available for them: it hands unused space back to the drive and "
        "changes nothing the databases hold."
    );
    return descriptor;
}
}
