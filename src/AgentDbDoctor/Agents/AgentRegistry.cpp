// The implementation of the locator and the registry.
//
// The registry is where composition is put to work. It holds one instance of each
// of the six agent classes as a field, and every method here asks one of those
// instances to describe itself. Nothing is derived from anything.
#include <filesystem>
// Provides `directory_iterator`, which lists the entries of one directory.
#include <filesystem>
// Provides `tolower`, the C library function that lowercases one character.
#include <cctype>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the locator, the descriptor types, and the registry declaration.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides the ten agent classes that the registry holds.
#include "AgentDbDoctor/Agents/OpenCodeAgent.hpp"
#include "AgentDbDoctor/Agents/CodexAgent.hpp"
#include "AgentDbDoctor/Agents/ClaudeCodeAgent.hpp"
#include "AgentDbDoctor/Agents/CursorAgent.hpp"
#include "AgentDbDoctor/Agents/ContinueAgent.hpp"
#include "AgentDbDoctor/Agents/AiderAgent.hpp"
#include "AgentDbDoctor/Agents/ClineAgent.hpp"
#include "AgentDbDoctor/Agents/RooCodeAgent.hpp"
#include "AgentDbDoctor/Agents/ReasonixAgent.hpp"
#include "AgentDbDoctor/Agents/VscodeAgent.hpp"
// Provides PlatformMetadata, used to read each candidate's metadata.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
optional<string> AgentDatabaseLocator::findFirstExistingPath() const {
    // Every candidate costs exactly one metadata call, and no directory is ever
    // opened. That is principle I/O-19 of the specification: file discovery is a
    // fixed list of paths, not a walk.
    for (const string& currentCandidatePath : candidatePathList) {
        const FileMetadata candidateStatus=PlatformMetadata::readFileMetadata(currentCandidatePath);
        if (candidateStatus.exists) {
            return currentCandidatePath;
        }
    }
    return nullopt;
}
vector<string> AgentDatabaseLocator::findAllExistingPaths() const {
    vector<string> foundPaths;
    for (const string& currentCandidatePath : candidatePathList) {
        const FileMetadata candidateStatus=PlatformMetadata::readFileMetadata(currentCandidatePath);
        if (candidateStatus.exists) {
            foundPaths.push_back(currentCandidatePath);
        }
    }
    return foundPaths;
}
const vector<string>& AgentDatabaseLocator::candidatePaths() const {
    return candidatePathList;
}
size_t AgentDatabaseLocator::candidateCount() const {
    return candidatePathList.size();
}
string AgentRegistry::joinPath(const string& basePath, const string& leafPath) {
    if (basePath.empty()) {
        return leafPath;
    }
    if (leafPath.empty()) {
        return basePath;
    }
    // A forward slash is the separator on all three platforms. Windows accepts a
    // forward slash in every file API it exposes, so one separator means one code
    // path and no conditional.
    const bool baseEndsWithSeparator=basePath.back()=='/' || basePath.back()=='\\';
    if (baseEndsWithSeparator) {
        return basePath + leafPath;
    }
    return basePath + "/" + leafPath;
}
vector<string> AgentRegistry::directoryEntryNames(const string& directoryPath, int maximumEntryCount) {
    vector<string> entryNames;
    if (maximumEntryCount<=0) {
        return entryNames;
    }
    std::error_code ignoredError;
    // A non recursive directory iterator lists exactly one level and stops. It is
    // the only directory listing in the tool, and it exists because Cursor and
    // Claude Code write into folders named after a hash or a random identifier
    // that no fixed candidate list can predict. The `error_code` overload is used
    // because the throwing overload would turn a permission problem into an
    // exception during a routine diagnostic pass.
    for (const auto& currentEntry : std::filesystem::directory_iterator(
        directoryPath,
        std::filesystem::directory_options::none,
        ignoredError
    )) {
        entryNames.push_back(currentEntry.path().filename().string());
        if (static_cast<int>(entryNames.size())>=maximumEntryCount) {
            break;
        }
    }
    return entryNames;
}
AgentRegistry::AgentRegistry()
    : openCodeAgent(make_unique<OpenCodeAgent>()),
      codexAgent(make_unique<CodexAgent>()),
      claudeCodeAgent(make_unique<ClaudeCodeAgent>()),
      cursorAgent(make_unique<CursorAgent>()),
      continueAgent(make_unique<ContinueAgent>()),
      aiderAgent(make_unique<AiderAgent>()),
      clineAgent(make_unique<ClineAgent>()),
      rooCodeAgent(make_unique<RooCodeAgent>()),
      reasonixAgent(make_unique<ReasonixAgent>()),
      vscodeAgent(make_unique<VscodeAgent>()) {
}
AgentRegistry::~AgentRegistry() = default;
vector<AgentDescriptor> AgentRegistry::allAgents() const {
    // The order is fixed and does not depend on the machine, so two reports can
    // be compared line by line.
    vector<AgentDescriptor> descriptors;
    descriptors.push_back(openCodeAgent->describe());
    descriptors.push_back(cursorAgent->describe());
    descriptors.push_back(codexAgent->describe());
    descriptors.push_back(claudeCodeAgent->describe());
    descriptors.push_back(continueAgent->describe());
    descriptors.push_back(aiderAgent->describe());
    descriptors.push_back(clineAgent->describe());
    descriptors.push_back(rooCodeAgent->describe());
    descriptors.push_back(reasonixAgent->describe());
    descriptors.push_back(vscodeAgent->describe());
    return descriptors;
}
vector<AgentDescriptor> AgentRegistry::sqliteAgents() const {
    vector<AgentDescriptor> filtered;
    for (const AgentDescriptor& currentDescriptor : allAgents()) {
        if (currentDescriptor.usesSqliteDatabase) {
            filtered.push_back(currentDescriptor);
        }
    }
    return filtered;
}
vector<AgentDescriptor> AgentRegistry::prunableAgents() const {
    vector<AgentDescriptor> filtered;
    for (const AgentDescriptor& currentDescriptor : allAgents()) {
        if (currentDescriptor.supportsPruning) {
            filtered.push_back(currentDescriptor);
        }
    }
    return filtered;
}
optional<AgentDescriptor> AgentRegistry::findAgent(const string& displayName) const {
    for (const AgentDescriptor& currentDescriptor : allAgents()) {
        if (currentDescriptor.displayName==displayName) {
            return currentDescriptor;
        }
    }
    // A second pass compares without regard to capitalisation, so a user typing
    // `opencode` finds `OpenCode` instead of being told nothing matches.
    string requestedName=displayName;
    for (char& currentCharacter : requestedName) {
        // `tolower` from the C library returns an `int`, and the cast narrows it
        // back to `char`. Only ASCII letters are affected, which is enough for the
        // product names this tool deals with.
        currentCharacter=static_cast<char>(tolower(static_cast<unsigned char>(currentCharacter)));
    }
    for (const AgentDescriptor& currentDescriptor : allAgents()) {
        string candidateName=currentDescriptor.displayName;
        for (char& currentCharacter : candidateName) {
            currentCharacter=static_cast<char>(tolower(static_cast<unsigned char>(currentCharacter)));
        }
        if (candidateName==requestedName) {
            return currentDescriptor;
        }
    }
    return nullopt;
}
vector<DiscoveredDatabase> AgentRegistry::discoverAllDatabases() const {
    vector<DiscoveredDatabase> discoveredDatabases;
    // The OpenCode database is the common case, and it is found by testing a fixed
    // list of candidate paths.
    const optional<string> openCodePath=openCodeAgent->findDatabasePath();
    if (openCodePath.has_value()) {
        const FileMetadata status=PlatformMetadata::readFileMetadata(openCodePath.value());
        DiscoveredDatabase discovered;
        discovered.agentName=openCodeAgent->displayName();
        discovered.filePath=openCodePath.value();
        discovered.isSqliteDatabase=true;
        discovered.sizeBytes=status.logicalSizeBytes;
        discoveredDatabases.push_back(discovered);
    }
    // The text based agents are reported by their folder rather than by a single
    // file, because a folder is what a user would recognise. Each is handled
    // explicitly rather than through a loop over a common base class, because the
    // specification forbids inheritance and there is no shared type to loop over.
    // The lambda holds one short piece of shared behaviour rather than a base class
    // with one method.
    const auto addFolderEntry = [this, &discoveredDatabases](const string& agentName, const optional<string>& folderPath) {
        if (!folderPath.has_value()) {
            return;
        }
        const FileMetadata status=PlatformMetadata::readFileMetadata(folderPath.value());
        if (!status.exists) {
            return;
        }
        DiscoveredDatabase discovered;
        discovered.agentName=agentName;
        discovered.filePath=folderPath.value();
        discovered.isSqliteDatabase=false;
        discovered.sizeBytes=status.logicalSizeBytes;
        discoveredDatabases.push_back(discovered);
    };
    addFolderEntry(codexAgent->displayName(), codexAgent->findDatabasePath());
    addFolderEntry(claudeCodeAgent->displayName(), claudeCodeAgent->findDatabasePath());
    addFolderEntry(aiderAgent->displayName(), aiderAgent->findDatabasePath());
    addFolderEntry(rooCodeAgent->displayName(), rooCodeAgent->findDatabasePath());
    addFolderEntry(reasonixAgent->displayName(), reasonixAgent->findDatabasePath());
    // Cline keeps a single SQLite file, so it is reported the way OpenCode is:
    // one path, one size, and the SQLite flag set.
    const optional<string> clinePath=clineAgent->findDatabasePath();
    if (clinePath.has_value()) {
        const FileMetadata clineStatus=PlatformMetadata::readFileMetadata(clinePath.value());
        if (clineStatus.exists && !clineStatus.isDirectory) {
            DiscoveredDatabase discovered;
            discovered.agentName=clineAgent->displayName();
            discovered.filePath=clinePath.value();
            discovered.isSqliteDatabase=true;
            discovered.sizeBytes=clineStatus.logicalSizeBytes;
            discoveredDatabases.push_back(discovered);
        }
    }
    // Cursor is added after the fixed list because finding it requires a bounded
    // directory listing, and the specification prefers a fixed candidate list
    // wherever one is possible.
    for (const DiscoveredDatabase& workspaceDatabase : cursorAgent->findWorkspaceDatabases(500)) {
        discoveredDatabases.push_back(workspaceDatabase);
    }
    // VS Code lists its workspace state the same way Cursor does, for the same
    // reason: the folder names are hashes no fixed list can predict.
    for (const DiscoveredDatabase& workspaceDatabase : vscodeAgent->findWorkspaceDatabases(500)) {
        discoveredDatabases.push_back(workspaceDatabase);
    }
    return discoveredDatabases;
}
string AgentRegistry::supportedAgentNames() const {
    // The names are joined into one sentence for the help text, so a user sees one
    // list rather than a table they have to scan.
    string joinedNames;
    const vector<AgentDescriptor> descriptors=allAgents();
    for (size_t descriptorIndex=0; descriptorIndex<descriptors.size(); ++descriptorIndex) {
        if (descriptorIndex>0) {
            joinedNames+=descriptorIndex+1==descriptors.size() ? ", and " : ", ";
        }
        joinedNames+=descriptors[descriptorIndex].displayName;
    }
    return joinedNames;
}
}
