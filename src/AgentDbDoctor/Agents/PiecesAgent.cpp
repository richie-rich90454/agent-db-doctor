// The implementation of the PiecesAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the PiecesAgent class declaration.
#include "AgentDbDoctor/Agents/PiecesAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
PiecesAgent::PiecesAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string PiecesAgent::displayName() const {
    return string("Pieces");
}
vector<string> PiecesAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // The documented location on every platform is under the home directory.
        // There is a single candidate, because the folder holds snippets and
        // assistant state rather than platform specific application data.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".pieces"));
    }
    return candidates;
}
optional<string> PiecesAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> PiecesAgent::redundantEventTypes() const {
    // Pieces keeps saved snippets and assistant state under its home folder and
    // never writes a session database, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> PiecesAgent::likelyProcessNames() const {
    return vector<string>{"pieces", "pieces.exe"};
}
AgentDescriptor PiecesAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsCompaction=false;
    descriptor.storageDescription=string(
        "Pieces keeps saved snippets and assistant state under its home folder. "
        "There are no duplicate snapshots to remove, so the tool reports the size and leaves the "
        "files alone."
    );
    return descriptor;
}
}
