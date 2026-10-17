// The implementation of the locator and the registry.
//
// The registry is where composition is put to work. It holds one instance of each
// of the sixty two agent classes as a field, and every method here asks one of
// those instances to describe itself. Nothing is derived from anything.
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
// Provides the sixty two agent classes that the registry holds.
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
#include "AgentDbDoctor/Agents/GeminiAgent.hpp"
#include "AgentDbDoctor/Agents/WindsurfAgent.hpp"
#include "AgentDbDoctor/Agents/ZedAgent.hpp"
#include "AgentDbDoctor/Agents/CopilotAgent.hpp"
#include "AgentDbDoctor/Agents/WarpAgent.hpp"
#include "AgentDbDoctor/Agents/KiloCodeAgent.hpp"
#include "AgentDbDoctor/Agents/AugmentAgent.hpp"
#include "AgentDbDoctor/Agents/TraeAgent.hpp"
#include "AgentDbDoctor/Agents/PearAIAgent.hpp"
#include "AgentDbDoctor/Agents/AmpAgent.hpp"
#include "AgentDbDoctor/Agents/CommandCodeAgent.hpp"
#include "AgentDbDoctor/Agents/PiAgent.hpp"
#include "AgentDbDoctor/Agents/VoidAgent.hpp"
#include "AgentDbDoctor/Agents/TabnineAgent.hpp"
#include "AgentDbDoctor/Agents/CodyAgent.hpp"
#include "AgentDbDoctor/Agents/CodeiumAgent.hpp"
#include "AgentDbDoctor/Agents/SupermavenAgent.hpp"
#include "AgentDbDoctor/Agents/BitoAgent.hpp"
#include "AgentDbDoctor/Agents/CodeGPTAgent.hpp"
#include "AgentDbDoctor/Agents/BlackboxAgent.hpp"
#include "AgentDbDoctor/Agents/AskCodiAgent.hpp"
#include "AgentDbDoctor/Agents/PhindAgent.hpp"
#include "AgentDbDoctor/Agents/IntelliCodeAgent.hpp"
#include "AgentDbDoctor/Agents/TabbyAgent.hpp"
#include "AgentDbDoctor/Agents/PiecesAgent.hpp"
#include "AgentDbDoctor/Agents/AmazonQAgent.hpp"
#include "AgentDbDoctor/Agents/JunieAgent.hpp"
#include "AgentDbDoctor/Agents/QoderAgent.hpp"
#include "AgentDbDoctor/Agents/LingmaAgent.hpp"
#include "AgentDbDoctor/Agents/ComateAgent.hpp"
#include "AgentDbDoctor/Agents/DroidAgent.hpp"
#include "AgentDbDoctor/Agents/GitLabDuoAgent.hpp"
#include "AgentDbDoctor/Agents/WorkbuddyAgent.hpp"
#include "AgentDbDoctor/Agents/KimiAgent.hpp"
#include "AgentDbDoctor/Agents/KiroAgent.hpp"
#include "AgentDbDoctor/Agents/QwenCodeAgent.hpp"
#include "AgentDbDoctor/Agents/CodeBuddyAgent.hpp"
#include "AgentDbDoctor/Agents/MarsCodeAgent.hpp"
#include "AgentDbDoctor/Agents/QodoAgent.hpp"
#include "AgentDbDoctor/Agents/SourceryAgent.hpp"
#include "AgentDbDoctor/Agents/SnykAgent.hpp"
#include "AgentDbDoctor/Agents/SonarQubeAgent.hpp"
#include "AgentDbDoctor/Agents/AnythingLLMAgent.hpp"
#include "AgentDbDoctor/Agents/GPT4AllAgent.hpp"
#include "AgentDbDoctor/Agents/JanAgent.hpp"
#include "AgentDbDoctor/Agents/LMStudioAgent.hpp"
#include "AgentDbDoctor/Agents/OpenWebUIAgent.hpp"
#include "AgentDbDoctor/Agents/AIChatAgent.hpp"
#include "AgentDbDoctor/Agents/GptmeAgent.hpp"
#include "AgentDbDoctor/Agents/LlmAgent.hpp"
#include "AgentDbDoctor/Agents/ShellGPTAgent.hpp"
#include "AgentDbDoctor/Agents/RaycastAgent.hpp"
// Provides PlatformMetadata, used to read each candidate's metadata.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Agents;
namespace AgentDbDoctor::Agents {
// How many entries are listed inside a Continue configuration folder, and the
// size above which one is worth reporting. The listing bound keeps the number of
// system calls fixed; the threshold exists because a vector index can reach
// gigabytes while everything else in the folder is noise.
static constexpr int CONTINUE_INDEX_LISTING_LIMIT=2000;
static constexpr long long CONTINUE_INDEX_MINIMUM_REPORTING_BYTES=10LL*1024LL*1024LL;
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
      vscodeAgent(make_unique<VscodeAgent>()),
      geminiAgent(make_unique<GeminiAgent>()),
      windsurfAgent(make_unique<WindsurfAgent>()),
      zedAgent(make_unique<ZedAgent>()),
      copilotAgent(make_unique<CopilotAgent>()),
      warpAgent(make_unique<WarpAgent>()),
      kiloCodeAgent(make_unique<KiloCodeAgent>()),
      augmentAgent(make_unique<AugmentAgent>()),
      traeAgent(make_unique<TraeAgent>()),
      pearAIAgent(make_unique<PearAIAgent>()),
      ampAgent(make_unique<AmpAgent>()),
      commandCodeAgent(make_unique<CommandCodeAgent>()),
      piAgent(make_unique<PiAgent>()),
      voidAgent(make_unique<VoidAgent>()),
      tabnineAgent(make_unique<TabnineAgent>()),
      codyAgent(make_unique<CodyAgent>()),
      codeiumAgent(make_unique<CodeiumAgent>()),
      supermavenAgent(make_unique<SupermavenAgent>()),
      bitoAgent(make_unique<BitoAgent>()),
      codeGPTAgent(make_unique<CodeGPTAgent>()),
      blackboxAgent(make_unique<BlackboxAgent>()),
      askCodiAgent(make_unique<AskCodiAgent>()),
      phindAgent(make_unique<PhindAgent>()),
      intelliCodeAgent(make_unique<IntelliCodeAgent>()),
      tabbyAgent(make_unique<TabbyAgent>()),
      piecesAgent(make_unique<PiecesAgent>()),
      amazonQAgent(make_unique<AmazonQAgent>()),
      junieAgent(make_unique<JunieAgent>()),
      qoderAgent(make_unique<QoderAgent>()),
      lingmaAgent(make_unique<LingmaAgent>()),
      comateAgent(make_unique<ComateAgent>()),
      droidAgent(make_unique<DroidAgent>()),
      gitLabDuoAgent(make_unique<GitLabDuoAgent>()),
      workbuddyAgent(make_unique<WorkbuddyAgent>()),
      kimiAgent(make_unique<KimiAgent>()),
      kiroAgent(make_unique<KiroAgent>()),
      qwenCodeAgent(make_unique<QwenCodeAgent>()),
      codeBuddyAgent(make_unique<CodeBuddyAgent>()),
      marsCodeAgent(make_unique<MarsCodeAgent>()),
      qodoAgent(make_unique<QodoAgent>()),
      sourceryAgent(make_unique<SourceryAgent>()),
      snykAgent(make_unique<SnykAgent>()),
      sonarQubeAgent(make_unique<SonarQubeAgent>()),
      anythingLLMAgent(make_unique<AnythingLLMAgent>()),
      gpt4AllAgent(make_unique<GPT4AllAgent>()),
      janAgent(make_unique<JanAgent>()),
      lmStudioAgent(make_unique<LMStudioAgent>()),
      openWebUIAgent(make_unique<OpenWebUIAgent>()),
      aiChatAgent(make_unique<AIChatAgent>()),
      gptmeAgent(make_unique<GptmeAgent>()),
      llmAgent(make_unique<LlmAgent>()),
      shellGPTAgent(make_unique<ShellGPTAgent>()),
      raycastAgent(make_unique<RaycastAgent>()) {
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
    descriptors.push_back(geminiAgent->describe());
    descriptors.push_back(windsurfAgent->describe());
    descriptors.push_back(zedAgent->describe());
    descriptors.push_back(copilotAgent->describe());
    descriptors.push_back(warpAgent->describe());
    descriptors.push_back(kiloCodeAgent->describe());
    descriptors.push_back(augmentAgent->describe());
    descriptors.push_back(traeAgent->describe());
    descriptors.push_back(pearAIAgent->describe());
    descriptors.push_back(ampAgent->describe());
    descriptors.push_back(commandCodeAgent->describe());
    descriptors.push_back(piAgent->describe());
    descriptors.push_back(voidAgent->describe());
    descriptors.push_back(tabnineAgent->describe());
    descriptors.push_back(codyAgent->describe());
    descriptors.push_back(codeiumAgent->describe());
    descriptors.push_back(supermavenAgent->describe());
    descriptors.push_back(bitoAgent->describe());
    descriptors.push_back(codeGPTAgent->describe());
    descriptors.push_back(blackboxAgent->describe());
    descriptors.push_back(askCodiAgent->describe());
    descriptors.push_back(phindAgent->describe());
    descriptors.push_back(intelliCodeAgent->describe());
    descriptors.push_back(tabbyAgent->describe());
    descriptors.push_back(piecesAgent->describe());
    descriptors.push_back(amazonQAgent->describe());
    descriptors.push_back(junieAgent->describe());
    descriptors.push_back(qoderAgent->describe());
    descriptors.push_back(lingmaAgent->describe());
    descriptors.push_back(comateAgent->describe());
    descriptors.push_back(droidAgent->describe());
    descriptors.push_back(gitLabDuoAgent->describe());
    descriptors.push_back(workbuddyAgent->describe());
    descriptors.push_back(kimiAgent->describe());
    descriptors.push_back(kiroAgent->describe());
    descriptors.push_back(qwenCodeAgent->describe());
    descriptors.push_back(codeBuddyAgent->describe());
    descriptors.push_back(marsCodeAgent->describe());
    descriptors.push_back(qodoAgent->describe());
    descriptors.push_back(sourceryAgent->describe());
    descriptors.push_back(snykAgent->describe());
    descriptors.push_back(sonarQubeAgent->describe());
    descriptors.push_back(anythingLLMAgent->describe());
    descriptors.push_back(gpt4AllAgent->describe());
    descriptors.push_back(janAgent->describe());
    descriptors.push_back(lmStudioAgent->describe());
    descriptors.push_back(openWebUIAgent->describe());
    descriptors.push_back(aiChatAgent->describe());
    descriptors.push_back(gptmeAgent->describe());
    descriptors.push_back(llmAgent->describe());
    descriptors.push_back(shellGPTAgent->describe());
    descriptors.push_back(raycastAgent->describe());
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
    const auto addFolderEntry = [&discoveredDatabases](const string& agentName, const optional<string>& folderPath) {
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
    // Continue's configuration folder can hold a code index that grows to
    // gigabytes. The folder is reported like the other text based agents, and in
    // addition every file inside it above the reporting threshold is listed on
    // its own, because the index is the part that actually grows. The listing is
    // one level deep and bounded, so it costs a fixed number of system calls.
    addFolderEntry(continueAgent->displayName(), continueAgent->findDatabasePath());
    for (const DiscoveredDatabase& indexFile : continueAgent->findLargeIndexFiles(
            CONTINUE_INDEX_LISTING_LIMIT,
            CONTINUE_INDEX_MINIMUM_REPORTING_BYTES)) {
        discoveredDatabases.push_back(indexFile);
    }
    addFolderEntry(rooCodeAgent->displayName(), rooCodeAgent->findDatabasePath());
    addFolderEntry(reasonixAgent->displayName(), reasonixAgent->findDatabasePath());
    addFolderEntry(geminiAgent->displayName(), geminiAgent->findDatabasePath());
    addFolderEntry(windsurfAgent->displayName(), windsurfAgent->findDatabasePath());
    addFolderEntry(zedAgent->displayName(), zedAgent->findDatabasePath());
    addFolderEntry(copilotAgent->displayName(), copilotAgent->findDatabasePath());
    addFolderEntry(warpAgent->displayName(), warpAgent->findDatabasePath());
    addFolderEntry(kiloCodeAgent->displayName(), kiloCodeAgent->findDatabasePath());
    addFolderEntry(augmentAgent->displayName(), augmentAgent->findDatabasePath());
    addFolderEntry(traeAgent->displayName(), traeAgent->findDatabasePath());
    addFolderEntry(pearAIAgent->displayName(), pearAIAgent->findDatabasePath());
    addFolderEntry(ampAgent->displayName(), ampAgent->findDatabasePath());
    addFolderEntry(commandCodeAgent->displayName(), commandCodeAgent->findDatabasePath());
    addFolderEntry(piAgent->displayName(), piAgent->findDatabasePath());
    addFolderEntry(voidAgent->displayName(), voidAgent->findDatabasePath());
    addFolderEntry(tabnineAgent->displayName(), tabnineAgent->findDatabasePath());
    addFolderEntry(codyAgent->displayName(), codyAgent->findDatabasePath());
    addFolderEntry(codeiumAgent->displayName(), codeiumAgent->findDatabasePath());
    addFolderEntry(supermavenAgent->displayName(), supermavenAgent->findDatabasePath());
    addFolderEntry(bitoAgent->displayName(), bitoAgent->findDatabasePath());
    addFolderEntry(codeGPTAgent->displayName(), codeGPTAgent->findDatabasePath());
    addFolderEntry(blackboxAgent->displayName(), blackboxAgent->findDatabasePath());
    addFolderEntry(askCodiAgent->displayName(), askCodiAgent->findDatabasePath());
    addFolderEntry(phindAgent->displayName(), phindAgent->findDatabasePath());
    addFolderEntry(intelliCodeAgent->displayName(), intelliCodeAgent->findDatabasePath());
    addFolderEntry(tabbyAgent->displayName(), tabbyAgent->findDatabasePath());
    addFolderEntry(piecesAgent->displayName(), piecesAgent->findDatabasePath());
    addFolderEntry(amazonQAgent->displayName(), amazonQAgent->findDatabasePath());
    addFolderEntry(junieAgent->displayName(), junieAgent->findDatabasePath());
    addFolderEntry(qoderAgent->displayName(), qoderAgent->findDatabasePath());
    addFolderEntry(lingmaAgent->displayName(), lingmaAgent->findDatabasePath());
    addFolderEntry(comateAgent->displayName(), comateAgent->findDatabasePath());
    addFolderEntry(droidAgent->displayName(), droidAgent->findDatabasePath());
    addFolderEntry(gitLabDuoAgent->displayName(), gitLabDuoAgent->findDatabasePath());
    addFolderEntry(workbuddyAgent->displayName(), workbuddyAgent->findDatabasePath());
    addFolderEntry(kimiAgent->displayName(), kimiAgent->findDatabasePath());
    addFolderEntry(kiroAgent->displayName(), kiroAgent->findDatabasePath());
    addFolderEntry(qwenCodeAgent->displayName(), qwenCodeAgent->findDatabasePath());
    addFolderEntry(codeBuddyAgent->displayName(), codeBuddyAgent->findDatabasePath());
    addFolderEntry(marsCodeAgent->displayName(), marsCodeAgent->findDatabasePath());
    addFolderEntry(qodoAgent->displayName(), qodoAgent->findDatabasePath());
    addFolderEntry(sourceryAgent->displayName(), sourceryAgent->findDatabasePath());
    addFolderEntry(snykAgent->displayName(), snykAgent->findDatabasePath());
    addFolderEntry(sonarQubeAgent->displayName(), sonarQubeAgent->findDatabasePath());
    addFolderEntry(anythingLLMAgent->displayName(), anythingLLMAgent->findDatabasePath());
    addFolderEntry(gpt4AllAgent->displayName(), gpt4AllAgent->findDatabasePath());
    addFolderEntry(janAgent->displayName(), janAgent->findDatabasePath());
    addFolderEntry(lmStudioAgent->displayName(), lmStudioAgent->findDatabasePath());
    addFolderEntry(openWebUIAgent->displayName(), openWebUIAgent->findDatabasePath());
    addFolderEntry(aiChatAgent->displayName(), aiChatAgent->findDatabasePath());
    addFolderEntry(gptmeAgent->displayName(), gptmeAgent->findDatabasePath());
    addFolderEntry(llmAgent->displayName(), llmAgent->findDatabasePath());
    addFolderEntry(shellGPTAgent->displayName(), shellGPTAgent->findDatabasePath());
    addFolderEntry(raycastAgent->displayName(), raycastAgent->findDatabasePath());
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
