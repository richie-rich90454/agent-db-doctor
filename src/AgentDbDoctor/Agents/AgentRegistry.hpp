// The agent catalogue: the locator that finds a database, the descriptor that
// describes one coding agent, and the registry that knows all of them.
//
// The specification forbids inheritance, so an agent is not a subclass of
// something called `Agent`. Each supported agent is its own small final class
// that contains an `AgentDatabaseLocator` and knows its own paths, its own
// redundant event type names, and its own process names. The registry holds one
// instance of each and asks each for a descriptor.
#ifndef AGENT_DB_DOCTOR_AGENTS_AGENTREGISTRY_HPP
#define AGENT_DB_DOCTOR_AGENTS_AGENTREGISTRY_HPP
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `memory`, which owns `unique_ptr` and `make_unique`.
#include <memory>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Agents {
// The supported agents are named here but not defined. A forward declaration
// lets a pointer to a class exist before the class itself has been read, which is
// what keeps the include graph free of cycles.
class OpenCodeAgent;
class CodexAgent;
class ClaudeCodeAgent;
class CursorAgent;
class ContinueAgent;
class AiderAgent;
class ClineAgent;
class RooCodeAgent;
class ReasonixAgent;
class VscodeAgent;
class GeminiAgent;
class WindsurfAgent;
class ZedAgent;
class CopilotAgent;
class WarpAgent;
class KiloCodeAgent;
class AugmentAgent;
class TraeAgent;
class PearAIAgent;
class AmpAgent;
class CommandCodeAgent;
class PiAgent;
class VoidAgent;
class TabnineAgent;
class CodyAgent;
class CodeiumAgent;
class SupermavenAgent;
class BitoAgent;
class CodeGPTAgent;
class BlackboxAgent;
class AskCodiAgent;
class PhindAgent;
class IntelliCodeAgent;
class TabbyAgent;
class PiecesAgent;
class AmazonQAgent;
class JunieAgent;
class QoderAgent;
class LingmaAgent;
class ComateAgent;
class DroidAgent;
class GitLabDuoAgent;
class WorkbuddyAgent;
class KimiAgent;
class KiroAgent;
class QwenCodeAgent;
class CodeBuddyAgent;
class MarsCodeAgent;
class QodoAgent;
class SourceryAgent;
class SnykAgent;
class SonarQubeAgent;
class AnythingLLMAgent;
class GPT4AllAgent;
class JanAgent;
class LMStudioAgent;
class OpenWebUIAgent;
class AIChatAgent;
class GptmeAgent;
class LlmAgent;
class ShellGPTAgent;
class RaycastAgent;
// The AgentDatabaseLocator class finds the first existing path from a fixed list
// of candidates.
//
// The class is `final` because the specification forbids inheritance. It exists
// as a separate class precisely to demonstrate composition: the agent classes
// contain one of these rather than deriving from it, and the registry contains
// sixty two agents rather than deriving anything from them.
class AgentDatabaseLocator final {
public:
    // Stores the candidate paths in the order they should be tried. The order
    // matters: the first candidate is the documented location, and the rest are
    // fallbacks for other platforms and older releases.
    explicit AgentDatabaseLocator(vector<string> candidatePaths)
        : candidatePathList(std::move(candidatePaths)) {
    }
    // Returns the first candidate that exists, or `nullopt` when none of them do.
    // Each candidate is tested with one metadata call, and no directory is ever
    // walked, which is principle I/O-19 of the specification.
    optional<string> findFirstExistingPath() const;
    // Returns every candidate that exists, in the order they were given. Used for
    // agents that store more than one file, such as a session folder.
    vector<string> findAllExistingPaths() const;
    // The candidate list, for reporting and for tests.
    const vector<string>& candidatePaths() const;
    // How many candidates were configured.
    size_t candidateCount() const;
private:
    // The field is not called `candidatePaths` because C++ forbids a field and a
    // method sharing one name, and the reading method keeps the natural name.
    vector<string> candidatePathList;
};
// The AgentDescriptor value type holds everything the rest of the tool needs to
// know about one coding agent.
//
// It is a plain value returned by value, not a base class, which is how the
// codebase shares behaviour without inheritance.
struct AgentDescriptor final {
    // The name shown to the user, for example "OpenCode".
    string displayName;
    // Where this agent keeps its data on this platform, in the order the paths
    // should be tried.
    vector<string> candidatePaths;
    // The event type names that mark a row as a redundant snapshot. This is empty
    // for agents that store sessions as plain text files, because there is
    // nothing to prune.
    vector<string> redundantEventTypes;
    // The names of the programs most likely to be holding the database open, used
    // to tell the user what to close.
    vector<string> likelyProcessNames;
    // True when this agent keeps its history in a SQLite database, which is the
    // only case where the prune and compact commands apply.
    bool usesSqliteDatabase=false;
    // True when this agent's data can be reduced without losing conversation
    // content. Only true for agents that store redundant snapshots.
    bool supportsPruning=false;
    // A sentence saying where this agent's data is, for the report.
    string storageDescription;
};
// The DiscoveredDatabase value type pairs an agent with a file the tool found.
struct DiscoveredDatabase final {
    // The name of the agent that owns the file.
    string agentName;
    // The full path to the file.
    string filePath;
    // True when the file is a SQLite database rather than a text file.
    bool isSqliteDatabase=false;
    // The logical size in bytes, read with one metadata call.
    long long sizeBytes=0;
};
// The AgentRegistry class holds one instance of every supported agent and asks
// each of them to describe itself.
//
// The class is `final` because the specification forbids inheritance. It contains
// the sixty two agent classes as fields, which is the composition the
// specification requires in place of a common base class.
class AgentRegistry final {
public:
    // Builds a registry holding one instance of every supported agent.
    AgentRegistry();
    // Releases the agent instances. The body is written in the implementation
    // file, where the agent classes are fully defined, because a `unique_ptr` can
    // only be destroyed when its target type is complete.
    ~AgentRegistry();
    // Copying is forbidden because the agent instances own resources.
    AgentRegistry(const AgentRegistry&)=delete;
    AgentRegistry& operator=(const AgentRegistry&)=delete;
    // Moving is allowed, so a registry can be returned from a function without
    // copying.
    AgentRegistry(AgentRegistry&&) noexcept=default;
    AgentRegistry& operator=(AgentRegistry&&) noexcept=default;
    // The descriptor for every supported agent, in a fixed order that does not
    // change between runs, so that reports are comparable.
    vector<AgentDescriptor> allAgents() const;
    // The descriptors for the agents that keep their history in a SQLite
    // database, which are the only ones the prune and compact commands act on.
    vector<AgentDescriptor> sqliteAgents() const;
    // The descriptors for the agents that can be pruned.
    vector<AgentDescriptor> prunableAgents() const;
    // The descriptor for the agent with the given name, or `nullopt` when no such
    // agent is supported. The comparison ignores case, because a user typing a
    // name on a command line should not have to match capitalisation.
    optional<AgentDescriptor> findAgent(const string& displayName) const;
    // Looks for the data of every supported agent and returns what was found.
    //
    // Discovery never walks a directory tree. Each agent contributes a fixed list
    // of candidate paths, and each candidate costs one metadata call. The one
    // exception is documented on the method that performs the bounded listing.
    vector<DiscoveredDatabase> discoverAllDatabases() const;
    // The names of every supported agent, joined with commas, for help text.
    string supportedAgentNames() const;
    // Joins two path fragments with a forward slash.
    //
    // Windows accepts forward slashes in every file API it has, so one separator
    // works on all three platforms and no conditional is needed.
    static string joinPath(const string& basePath, const string& leafPath);
    // Lists the names of the entries in one directory, stopping after
    // `maximumEntryCount` names.
    //
    // This is the single place the tool lists a directory, and it exists only for
    // Cursor and Continue, whose files sit in a folder whose name is a random
    // per workspace identifier that no fixed candidate list can predict. The
    // listing is one directory deep and bounded by `maximumEntryCount`, so the
    // number of system calls is fixed rather than proportional to the size of the
    // tree. Everything else uses a fixed candidate list.
    static vector<string> directoryEntryNames(const string& directoryPath, int maximumEntryCount);
private:
    // The agent instances, one per supported coding agent. This is the
    // composition the specification requires in place of a common base class.
    //
    // Each is held as a `unique_ptr` because the header only forward declares the
    // agent classes, and a `unique_ptr` to a type that is not yet complete is
    // allowed as long as the class that owns it defines its destructor in a
    // translation unit where the type is complete. Holding them by value would
    // force every agent header to be included here, and every agent header needs
    // the descriptor declared above, which would be a circular dependency.
    //
    // Forward declaration is how C++ names a class without defining it, so the
    // pointer can point at one before its full definition has been read.
    unique_ptr<OpenCodeAgent> openCodeAgent;
    unique_ptr<CodexAgent> codexAgent;
    unique_ptr<ClaudeCodeAgent> claudeCodeAgent;
    unique_ptr<CursorAgent> cursorAgent;
    unique_ptr<ContinueAgent> continueAgent;
    unique_ptr<AiderAgent> aiderAgent;
    unique_ptr<ClineAgent> clineAgent;
    unique_ptr<RooCodeAgent> rooCodeAgent;
    unique_ptr<ReasonixAgent> reasonixAgent;
    unique_ptr<VscodeAgent> vscodeAgent;
    unique_ptr<GeminiAgent> geminiAgent;
    unique_ptr<WindsurfAgent> windsurfAgent;
    unique_ptr<ZedAgent> zedAgent;
    unique_ptr<CopilotAgent> copilotAgent;
    unique_ptr<WarpAgent> warpAgent;
    unique_ptr<KiloCodeAgent> kiloCodeAgent;
    unique_ptr<AugmentAgent> augmentAgent;
    unique_ptr<TraeAgent> traeAgent;
    unique_ptr<PearAIAgent> pearAIAgent;
    unique_ptr<AmpAgent> ampAgent;
    unique_ptr<CommandCodeAgent> commandCodeAgent;
    unique_ptr<PiAgent> piAgent;
    unique_ptr<VoidAgent> voidAgent;
    unique_ptr<TabnineAgent> tabnineAgent;
    unique_ptr<CodyAgent> codyAgent;
    unique_ptr<CodeiumAgent> codeiumAgent;
    unique_ptr<SupermavenAgent> supermavenAgent;
    unique_ptr<BitoAgent> bitoAgent;
    unique_ptr<CodeGPTAgent> codeGPTAgent;
    unique_ptr<BlackboxAgent> blackboxAgent;
    unique_ptr<AskCodiAgent> askCodiAgent;
    unique_ptr<PhindAgent> phindAgent;
    unique_ptr<IntelliCodeAgent> intelliCodeAgent;
    unique_ptr<TabbyAgent> tabbyAgent;
    unique_ptr<PiecesAgent> piecesAgent;
    unique_ptr<AmazonQAgent> amazonQAgent;
    unique_ptr<JunieAgent> junieAgent;
    unique_ptr<QoderAgent> qoderAgent;
    unique_ptr<LingmaAgent> lingmaAgent;
    unique_ptr<ComateAgent> comateAgent;
    unique_ptr<DroidAgent> droidAgent;
    unique_ptr<GitLabDuoAgent> gitLabDuoAgent;
    unique_ptr<WorkbuddyAgent> workbuddyAgent;
    unique_ptr<KimiAgent> kimiAgent;
    unique_ptr<KiroAgent> kiroAgent;
    unique_ptr<QwenCodeAgent> qwenCodeAgent;
    unique_ptr<CodeBuddyAgent> codeBuddyAgent;
    unique_ptr<MarsCodeAgent> marsCodeAgent;
    unique_ptr<QodoAgent> qodoAgent;
    unique_ptr<SourceryAgent> sourceryAgent;
    unique_ptr<SnykAgent> snykAgent;
    unique_ptr<SonarQubeAgent> sonarQubeAgent;
    unique_ptr<AnythingLLMAgent> anythingLLMAgent;
    unique_ptr<GPT4AllAgent> gpt4AllAgent;
    unique_ptr<JanAgent> janAgent;
    unique_ptr<LMStudioAgent> lmStudioAgent;
    unique_ptr<OpenWebUIAgent> openWebUIAgent;
    unique_ptr<AIChatAgent> aiChatAgent;
    unique_ptr<GptmeAgent> gptmeAgent;
    unique_ptr<LlmAgent> llmAgent;
    unique_ptr<ShellGPTAgent> shellGPTAgent;
    unique_ptr<RaycastAgent> raycastAgent;
};
}
#endif