// The implementation of the VoidAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the VoidAgent class declaration.
#include "AgentDbDoctor/Agents/VoidAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
VoidAgent::VoidAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string VoidAgent::displayName() const {
    return string("Void");
}
vector<string> VoidAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The data directory parent differs per platform, and the workspace
        // storage folder is listed too. All are listed so the tool works on a
        // machine set up for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".void"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Void"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Void"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Void"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Void/User/workspaceStorage"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Void/User/workspaceStorage"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Void/User/workspaceStorage"));
    }
    return candidates;
}
optional<string> VoidAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> VoidAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the session storage format is undocumented.
    // Returning an empty list is what makes the compact command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> VoidAgent::likelyProcessNames() const {
    return vector<string>{"Void", "Void.exe"};
}
AgentDescriptor VoidAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    // Void is a VS Code family editor: its workspace state lives in the same
    // SQLite state files the family uses, so the flag matches the other members
    // of the family. The tool still only reports, because the tables are
    // undocumented.
    descriptor.usesSqliteDatabase=true;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Void keeps session and workspace state in the editor's own SQLite state files "
        "under its data directory. The tool reports their sizes. It does not change them, "
        "because the format is undocumented. The vacuum command is always available for them: "
        "it hands unused space back to the drive and changes nothing the files hold."
    );
    return descriptor;
}
}
