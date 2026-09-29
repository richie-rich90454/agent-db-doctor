// The implementation of the OpenCodeAgent class.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the OpenCodeAgent class declaration.
#include "AgentDbDoctor/Agents/OpenCodeAgent.hpp"
// Provides PlatformMetadata, used to find the user's home and application data
// directories.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
// The event type names OpenCode writes for the intermediate snapshots.
//
// The version suffix is the whole point. When a message changes, OpenCode writes
// a new row describing the new state and leaves the old row in place. The old row
// is a complete duplicate of a state the conversation has already moved past, so
// deleting it loses nothing that the current row does not also contain. A `.1`
// suffix means "the first recorded version of this object".
//
// These are `constexpr` so the compiler places them in read only memory. Comparing
// them against a row's type is then a pointer comparison rather than a string
// comparison, which is principle P-9 of the specification.
static constexpr const char* EVENT_TYPE_MESSAGE_UPDATED="message.updated.1";
static constexpr const char* EVENT_TYPE_MESSAGE_PART_UPDATED="message.part.updated.1";
static constexpr const char* EVENT_TYPE_SESSION_UPDATED="session.updated.1";
OpenCodeAgent::OpenCodeAgent()
    : redundantTypeNames({
        string(EVENT_TYPE_MESSAGE_UPDATED),
        string(EVENT_TYPE_MESSAGE_PART_UPDATED),
        string(EVENT_TYPE_SESSION_UPDATED)
      }),
      databaseLocator(buildCandidatePaths()) {
}
string OpenCodeAgent::displayName() const {
    return string("OpenCode");
}
vector<string> OpenCodeAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    // The documented Linux and macOS location is the only one on those systems.
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(
            AgentRegistry::joinPath(homeDirectoryPath, ".local/share/opencode/opencode.db")
        );
    }
    // On Windows OpenCode follows the platform convention for application data.
    // `%LOCALAPPDATA%` is the per user, per machine location, which is where a
    // cache-like database belongs.
#if defined(_WIN32)
    string localAppData;
    if (PlatformMetadata::readEnvironmentVariable("LOCALAPPDATA", localAppData)) {
        candidates.push_back(AgentRegistry::joinPath(localAppData, "opencode/opencode.db"));
    }
    string roamingAppData;
    if (PlatformMetadata::readEnvironmentVariable("APPDATA", roamingAppData)) {
        candidates.push_back(AgentRegistry::joinPath(roamingAppData, "opencode/opencode.db"));
    }
    if (!homeDirectoryPath.empty()) {
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "AppData/Local/opencode/opencode.db"
        ));
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "AppData/Roaming/opencode/opencode.db"
        ));
    }
#endif
    // A local override is checked last so that a developer working on a test
    // database is never surprised by their real one.
    string overridePath;
    if (PlatformMetadata::readEnvironmentVariable("OPENCODE_DATABASE", overridePath)) {
        candidates.push_back(overridePath);
    }
    return candidates;
}
optional<string> OpenCodeAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> OpenCodeAgent::redundantEventTypes() const {
    return redundantTypeNames;
}
vector<string> OpenCodeAgent::likelyProcessNames() const {
    return vector<string>{"opencode", "OpenCode", "OpenCode.exe"};
}
AgentDescriptor OpenCodeAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=true;
    descriptor.supportsPruning=true;
    descriptor.storageDescription=string(
        "OpenCode keeps every session in one SQLite database. Each change to a message writes a new row "
        "and leaves the previous version behind, which is why the file grows much faster than the "
        "conversation does."
    );
    return descriptor;
}
}
