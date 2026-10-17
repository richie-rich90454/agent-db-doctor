// The implementation of the TabnineAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the TabnineAgent class declaration.
#include "AgentDbDoctor/Agents/TabnineAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
TabnineAgent::TabnineAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string TabnineAgent::displayName() const {
    return string("Tabnine");
}
vector<string> TabnineAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The extension storage parent differs per platform, so all three are
        // listed alongside the home folder. All are listed so the tool works
        // on a machine set up for a different platform.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".tabnine"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/Code/User/globalStorage/tabnine.tabnine-vscode"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".config/Code/User/globalStorage/tabnine.tabnine-vscode"));
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, "Library/Application Support/Code/User/globalStorage/tabnine.tabnine-vscode"));
    }
    return candidates;
}
optional<string> TabnineAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> TabnineAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the completion storage format is undocumented.
    // Returning an empty list is what makes the compact command refuse rather
    // than guess.
    return vector<string>();
}
vector<string> TabnineAgent::likelyProcessNames() const {
    return vector<string>{"Code.exe", "code"};
}
AgentDescriptor TabnineAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Tabnine keeps completion and chat state in its home folder plus editor extension storage. "
        "The tool reports the size and leaves the files alone."
    );
    return descriptor;
}
}
