// The implementation of the OpenCodeAgent class.
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
// The environment variable OpenCode itself reads for its database location.
//
// `path()` in `packages/core/src/database/database.ts` resolves `OPENCODE_DB`
// first: the value `:memory:` or an absolute path is used directly, and a
// relative name is joined with the data directory. `OPENCODE_DATABASE` is the
// older name this tool used before the real one was checked against the source,
// and it is still honoured as a fallback so that nobody's existing setup stops
// working when they upgrade.
static constexpr const char* ENVIRONMENT_VARIABLE_DATABASE="OPENCODE_DB";
static constexpr const char* ENVIRONMENT_VARIABLE_DATABASE_LEGACY="OPENCODE_DATABASE";
// Setting this to `1` or `true` makes OpenCode use `opencode.db` regardless of
// release channel. The same file honours it by skipping the channel variants.
static constexpr const char* ENVIRONMENT_VARIABLE_DISABLE_CHANNEL_DATABASE="OPENCODE_DISABLE_CHANNEL_DB";
// How many entries of a data directory are listed when looking for channel
// specific database files. One level only, the same bound the Cursor adapter
// uses for its workspace folders.
static constexpr int MAXIMUM_CHANNEL_FILE_COUNT=50;
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
    // The data directories, in the order OpenCode itself prefers them. The
    // `~/.local/share` entry applies on every platform: on this project's own
    // Windows machine the live 4.1 GB database sits there while the two
    // `%LOCALAPPDATA%` files are zero byte placeholders.
    vector<string> dataDirectories;
    if (!homeDirectoryPath.empty()) {
        dataDirectories.push_back(
            AgentRegistry::joinPath(homeDirectoryPath, ".local/share/opencode")
        );
    }
    vector<string> candidates;
    // The documented Linux and macOS location is the only one on those systems.
    for (const string& dataDirectory : dataDirectories) {
        candidates.push_back(AgentRegistry::joinPath(dataDirectory, "opencode.db"));
    }
    // On Windows OpenCode follows the platform convention for application data.
    // `%LOCALAPPDATA%` is the per user, per machine location, which is where a
    // cache-like database belongs.
#if defined(_WIN32)
    string localAppData;
    if (PlatformMetadata::readEnvironmentVariable("LOCALAPPDATA", localAppData)) {
        dataDirectories.push_back(AgentRegistry::joinPath(localAppData, "opencode"));
        candidates.push_back(AgentRegistry::joinPath(localAppData, "opencode/opencode.db"));
    }
    string roamingAppData;
    if (PlatformMetadata::readEnvironmentVariable("APPDATA", roamingAppData)) {
        dataDirectories.push_back(AgentRegistry::joinPath(roamingAppData, "opencode"));
        candidates.push_back(AgentRegistry::joinPath(roamingAppData, "opencode.db"));
    }
    if (!homeDirectoryPath.empty()) {
        dataDirectories.push_back(
            AgentRegistry::joinPath(homeDirectoryPath, "AppData/Local/opencode")
        );
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "AppData/Local/opencode/opencode.db"
        ));
        dataDirectories.push_back(
            AgentRegistry::joinPath(homeDirectoryPath, "AppData/Roaming/opencode")
        );
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            "AppData/Roaming/opencode/opencode.db"
        ));
    }
#endif
    // The override OpenCode itself honours, checked first so that a developer
    // working on a test database is never surprised by their real one. An
    // absolute value, or `:memory:`, names the database directly; anything else
    // is a file name inside the first data directory, exactly as OpenCode
    // resolves it.
    string overridePath;
    if (!PlatformMetadata::readEnvironmentVariable(
            ENVIRONMENT_VARIABLE_DATABASE, overridePath)
        && !PlatformMetadata::readEnvironmentVariable(
            ENVIRONMENT_VARIABLE_DATABASE_LEGACY, overridePath)) {
        overridePath.clear();
    }
    if (!overridePath.empty() && overridePath!=":memory:") {
        const bool isAbsolutePath=!overridePath.empty()
            && (overridePath.front()=='/'
                || (overridePath.size()>2 && overridePath[1]==':')
                || (overridePath.size()>1 && overridePath.front()=='\\'));
        if (isAbsolutePath) {
            candidates.insert(candidates.begin(), overridePath);
        } else if (!dataDirectories.empty()) {
            candidates.insert(
                candidates.begin(),
                AgentRegistry::joinPath(dataDirectories.front(), overridePath)
            );
        }
    }
    // OpenCode names the file after its release channel: `opencode.db` for the
    // `latest`, `beta`, and `prod` channels, and `opencode-{channel}.db` for
    // anything else. The channel names are not enumerated anywhere a tool can
    // read them, so the data directories are listed once, one level deep, for
    // files matching that shape. The flag that disables the behaviour is
    // honoured by skipping the listing.
    string disableChannelDatabase;
    const bool channelDatabaseDisabled=
        PlatformMetadata::readEnvironmentVariable(
            ENVIRONMENT_VARIABLE_DISABLE_CHANNEL_DATABASE, disableChannelDatabase)
        && (disableChannelDatabase=="1" || disableChannelDatabase=="true");
    if (!channelDatabaseDisabled) {
        for (const string& dataDirectory : dataDirectories) {
            for (const string& entryName :
                 AgentRegistry::directoryEntryNames(
                     dataDirectory, MAXIMUM_CHANNEL_FILE_COUNT)) {
                if (entryName.size()<=string("opencode-.db").size()
                    || entryName.compare(0, string("opencode-").size(), "opencode-")!=0
                    || entryName.compare(
                           entryName.size()-string(".db").size(),
                           string(".db").size(),
                           ".db")!=0) {
                    continue;
                }
                const string channelPath=
                    AgentRegistry::joinPath(dataDirectory, entryName);
                bool alreadyListed=false;
                for (const string& existingCandidate : candidates) {
                    if (existingCandidate==channelPath) {
                        alreadyListed=true;
                        break;
                    }
                }
                if (!alreadyListed) {
                    candidates.push_back(channelPath);
                }
            }
        }
    }
    return candidates;
}
optional<string> OpenCodeAgent::findDatabasePath() const {
    // A zero byte file is a placeholder, not a database: on Windows the two
    // `%LOCALAPPDATA%` candidates exist but are empty while the live database
    // sits elsewhere. The first candidate with a size is preferred, and only
    // when every existing candidate is empty is the first of them returned, so
    // that a fresh install still reports something rather than nothing.
    optional<string> firstExistingPath;
    for (const string& candidatePath : databaseLocator.candidatePaths()) {
        const FileMetadata candidateStatus=
            PlatformMetadata::readFileMetadata(candidatePath);
        if (!candidateStatus.exists) {
            continue;
        }
        if (!firstExistingPath.has_value()) {
            firstExistingPath=candidatePath;
        }
        if (!candidateStatus.isDirectory && candidateStatus.logicalSizeBytes>0) {
            return candidatePath;
        }
    }
    return firstExistingPath;
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
