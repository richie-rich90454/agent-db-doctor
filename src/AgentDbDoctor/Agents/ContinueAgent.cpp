// The implementation of the ContinueAgent class.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ContinueAgent class declaration.
#include "AgentDbDoctor/Agents/ContinueAgent.hpp"
// Provides PlatformMetadata, used to find the user's home directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
ContinueAgent::ContinueAgent()
    : databaseLocator(buildCandidatePaths()) {
}
string ContinueAgent::displayName() const {
    return string("Continue");
}
vector<string> ContinueAgent::buildCandidatePaths() {
    const string homeDirectoryPath=PlatformMetadata::homeDirectory();
    vector<string> candidates;
    if (!homeDirectoryPath.empty()) {
        // Continue is an extension, so it follows the extension host conventions
        // of the editor it runs inside. The dot folder is the documented default;
        // the editor specific folders cover the cases where the user changed it.
        candidates.push_back(AgentRegistry::joinPath(homeDirectoryPath, ".continue"));
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            ".vscode/extensions/continue.continue-*/data"
        ));
        candidates.push_back(AgentRegistry::joinPath(
            homeDirectoryPath,
            ".cursor/extensions/continue.continue-*/data"
        ));
    }
    return candidates;
}
optional<string> ContinueAgent::findDatabasePath() const {
    return databaseLocator.findFirstExistingPath();
}
vector<string> ContinueAgent::redundantEventTypes() const {
    // Continue stores configuration and an optional code index. Neither is a
    // snapshot history, so there is nothing that can be identified as redundant.
    return vector<string>();
}
vector<string> ContinueAgent::likelyProcessNames() const {
    return vector<string>{"Code", "Cursor", "Code.exe", "Cursor.exe"};
}
vector<DiscoveredDatabase> ContinueAgent::findLargeIndexFiles(
    int maximumFileCount,
    long long minimumSizeBytes
) const {
    vector<DiscoveredDatabase> largeFiles;
    const optional<string> configurationFolder=databaseLocator.findFirstExistingPath();
    if (!configurationFolder.has_value()) {
        return largeFiles;
    }
    // The listing is one level deep and bounded, so the number of system calls is
    // fixed rather than proportional to the size of the folder.
    const vector<string> entryNames=AgentRegistry::directoryEntryNames(
        configurationFolder.value(),
        maximumFileCount
    );
    for (const string& entryName : entryNames) {
        const string entryPath=AgentRegistry::joinPath(configurationFolder.value(), entryName);
        const FileMetadata entryStatus=PlatformMetadata::readFileMetadata(entryPath);
        if (!entryStatus.exists || entryStatus.isDirectory) {
            continue;
        }
        if (entryStatus.logicalSizeBytes < minimumSizeBytes) {
            continue;
        }
        DiscoveredDatabase discovered;
        discovered.agentName=displayName();
        discovered.filePath=entryPath;
        // The extension can store either a SQLite index or a plain text embedding
        // file, and the tool does not guess which. The diagnostic pass opens the
        // file and reports what it actually finds.
        discovered.isSqliteDatabase=false;
        discovered.sizeBytes=entryStatus.logicalSizeBytes;
        largeFiles.push_back(discovered);
        if (static_cast<int>(largeFiles.size())>=maximumFileCount) {
            break;
        }
    }
    return largeFiles;
}
AgentDescriptor ContinueAgent::describe() const {
    AgentDescriptor descriptor;
    descriptor.displayName=displayName();
    descriptor.candidatePaths=databaseLocator.candidatePaths();
    descriptor.redundantEventTypes=redundantEventTypes();
    descriptor.likelyProcessNames=likelyProcessNames();
    descriptor.usesSqliteDatabase=false;
    descriptor.supportsPruning=false;
    descriptor.storageDescription=string(
        "Continue keeps a configuration folder that can include a code index. The index can grow to "
        "gigabytes, so the tool reports its size. The tool does not prune it, because an index is "
        "rebuildable but its exact contents are not the tool's to decide."
    );
    return descriptor;
}
}
