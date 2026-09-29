// The implementation of the AiderAgent class.
#include <filesystem>
// Provides `current_path`, which reports the process working directory.
#include <filesystem>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AiderAgent class declaration.
#include "AgentDbDoctor/Agents/AiderAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
// The file Aider writes its Markdown transcript to. It is a dot file in the
// project directory, so it is normally hidden from a plain file listing.
static constexpr const char* HISTORY_FILE_NAME=".aider.chat.history.md";
// The folder Aider writes its repository index into, named after the release
// that produced it. The index is rebuilt automatically, so it is the one Aider
// file the tool can honestly describe as disposable.
static constexpr const char* INDEX_FOLDER_PREFIX=".aider.tags.cache.v";
AiderAgent::AiderAgent()
    : databaseLocator(buildCandidatePaths(string())) {
}
AiderAgent::AiderAgent(const string& projectDirectoryPath)
    : databaseLocator(buildCandidatePaths(projectDirectoryPath)) {
}
string AiderAgent::displayName() const {
    return string("Aider");
}
vector<string> AiderAgent::buildCandidatePaths(const string& projectDirectoryPath) {
    vector<string> candidates;
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    // Aider writes into whatever directory it was started in, so the project
    // directory the user named is the most likely place. When the user named none,
    // the process working directory is the best guess.
    const string directoryToSearch=projectDirectoryPath.empty()
        ? string(std::filesystem::current_path().string())
        : projectDirectoryPath;
    candidates.push_back(AgentRegistry::joinPath(directoryToSearch, HISTORY_FILE_NAME));
    if (!homeDirectoryPath.empty()) {
        // Aider can also be told to keep its history in the home directory, which
        // is what the documentation suggests for long running sessions.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, HISTORY_FILE_NAME));
    }
    return candidates;
}
optional<string> AiderAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> AiderAgent::redundantEventTypes() const {
    // Aider keeps a Markdown transcript, which has no duplicate snapshot rows.
    return vector<string>();
}
vector<string> AiderAgent::likelyProcessNames() const {
    return vector<string>{"aider"};
}
AgentDescriptor AiderAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Aider writes a Markdown transcript of the conversation into the project directory. "
        "The tool reports its size. Nothing is removed, because a transcript is the only copy of "
        "that conversation."
    );
    return descriptor;
}
}
