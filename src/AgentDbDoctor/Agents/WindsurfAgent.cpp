// The implementation of the WindsurfAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the WindsurfAgent class declaration.
#include "AgentDbDoctor/Agents/WindsurfAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
WindsurfAgent::WindsurfAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string WindsurfAgent::displayName() const {
    return string("Windsurf");
}
vector<string> WindsurfAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Windsurf follows the VS Code family layout, so every platform keeps
        // both a home dot folder and a workspace storage folder. All of them
        // are listed so the tool works on a machine set up for any platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".windsurf"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Windsurf"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Windsurf"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Windsurf"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Windsurf/User/workspaceStorage"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Windsurf/User/workspaceStorage"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Windsurf/User/workspaceStorage"));
    }
    return candidates;
}
optional<string> WindsurfAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> WindsurfAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the workspace state tables are undocumented.
    // Returning an empty list is what makes the compact command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> WindsurfAgent::likelyProcessNames() const {
    return vector<string>{"Windsurf", "Windsurf.exe"};
}
AgentDescriptor WindsurfAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    // Windsurf is a VS Code family editor: its workspace state lives in the same
    // SQLite state files the family uses, so the flag matches the other members
    // of the family. The tool still only reports, because the tables are
    // undocumented.
    descriptor.usesSqliteDatabase=true;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Windsurf keeps session and workspace state in the editor's own SQLite state files "
        "under its data directory. The tool reports their sizes. It does not change them, "
        "because the format is undocumented. The vacuum command is always available for them: "
        "it hands unused space back to the drive and changes nothing the files hold."
    );
    return descriptor;
}
}
