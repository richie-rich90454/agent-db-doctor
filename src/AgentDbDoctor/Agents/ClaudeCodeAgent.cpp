// The implementation of the ClaudeCodeAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ClaudeCodeAgent class declaration.
#include "AgentDbDoctor/Agents/ClaudeCodeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
// The suffix every Claude Code session file ends with. The file name also carries
// a random identifier before this suffix, so the name cannot be predicted and the
// directory has to be listed.
static constexpr const char* SESSION_FILE_SUFFIX=".jsonl";
ClaudeCodeAgent::ClaudeCodeAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string ClaudeCodeAgent::displayName() const {
    return string("Claude Code");
}
vector<string> ClaudeCodeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Claude Code keeps conversations in a `projects` folder under the home
        // directory, on every platform it supports.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".claude/projects"));
    }
    // An explicit override lets a developer point the tool at a test folder.
    string overridePath;
    if (PlatformMetadata::readEnvironmentVariable("CLAUDE_CONFIG_DIR", overridePath)) {
        candidates.push_back(AgentRegistry::joinPath(overridePath, "projects"));
    }
    return candidates;
}
optional<string> ClaudeCodeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> ClaudeCodeAgent::redundantEventTypes() const {
    // Claude Code writes one JSON object per line and never writes a second
    // version of a message, so there is nothing that can be identified as
    // redundant.
    return vector<string>();
}
vector<string> ClaudeCodeAgent::likelyProcessNames() const {
    return vector<string>{"claude"};
}
vector<string> ClaudeCodeAgent::findSessionFiles(int maximumFileCount) const {
    vector<string> sessionFilePaths;
    const optional<string> projectFolder=databaseLocator.findFirstExistingPath();
    if (!projectFolder.has_value()) {
        return sessionFilePaths;
    }
    // The listing is one level deep, which is the level Claude Code writes. The
    // project folder holds one subfolder per working directory, and the session
    // files are inside those. Each subfolder is listed, but the count is bounded
    // so the total number of system calls stays proportional to the bound rather
    // than to the size of the home directory.
    const vector<string> projectNames=AgentRegistry::directoryEntryNames(
        projectFolder.value(),
        maximumFileCount
    );
    for (const string& projectName : projectNames) {
        const string projectPath=AgentRegistry::joinPath(projectFolder.value(), projectName);
        const vector<string> entryNames=AgentRegistry::directoryEntryNames(
            projectPath,
            maximumFileCount
        );
        for (const string& entryName : entryNames) {
            if (entryName.size()>string(SESSION_FILE_SUFFIX).size()
                && entryName.compare(
                    entryName.size() - string(SESSION_FILE_SUFFIX).size(),
                    string(SESSION_FILE_SUFFIX).size(),
                    SESSION_FILE_SUFFIX
                )==0) {
                sessionFilePaths.push_back(AgentRegistry::joinPath(projectPath, entryName));
                if (static_cast<int>(sessionFilePaths.size())>=maximumFileCount) {
                    return sessionFilePaths;
                }
            }
        }
    }
    return sessionFilePaths;
}
AgentDescriptor ClaudeCodeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Claude Code writes one text file per conversation, with one JSON object on every line. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
