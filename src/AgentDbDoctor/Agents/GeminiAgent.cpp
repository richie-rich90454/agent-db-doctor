// The implementation of the GeminiAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the GeminiAgent class declaration.
#include "AgentDbDoctor/Agents/GeminiAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
GeminiAgent::GeminiAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string GeminiAgent::displayName() const {
    return string("Gemini CLI");
}
vector<string> GeminiAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // Per-project chats live below it in `tmp/<project-hash>/chats/`.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".gemini"));
    }
    return candidates;
}
optional<string> GeminiAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> GeminiAgent::redundantEventTypes() const {
    // The specification is explicit that this tool does not reverse engineer
    // undocumented schemas, and the chat files are plain session history with
    // no redundant snapshots. Returning an empty list is what makes the compact
    // command refuse rather than guess.
    return vector<string>();
}
vector<string> GeminiAgent::likelyProcessNames() const {
    return vector<string>{"gemini", "gemini.exe"};
}
AgentDescriptor GeminiAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "The Gemini CLI keeps per-project session history under `~/.gemini/tmp/<project-hash>/chats/`, "
        "with a 30-day retention policy. The tool reports the size and leaves the files alone."
    );
    return descriptor;
}
}
