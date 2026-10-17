// The unit tests for the agent catalogue.
//
// The catalogue is what makes the tool work across coding agents, so the tests are
// about two things: that a candidate path costs one metadata call and never a
// directory walk, and that every supported agent describes itself completely
// enough for a report to be written from it.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the catalogue and the sixty two agent classes.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
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
// Provides PlatformMetadata, whose counter is what the cost test watches.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::FileSystem;
TEST_CASE("a locator returns the first candidate that exists", "[agents]") {
    const AgentDatabaseLocator emptyLocator(vector<string>{
        "a path that does not exist",
        "another path that does not exist either"
    });
    // With nothing there, the answer is nothing rather than a wrong guess.
    REQUIRE_FALSE(emptyLocator.findFirstExistingPath().has_value());
    REQUIRE(emptyLocator.candidateCount()==2);
    const AgentDatabaseLocator mixedLocator(vector<string>{
        "a path that does not exist",
        "."
    });
    const optional<string> foundPath=mixedLocator.findFirstExistingPath();
    REQUIRE(foundPath.has_value());
    REQUIRE(foundPath.value()==".");
}
TEST_CASE("a locator tests each candidate once and never walks a tree", "[agents]") {
    AgentDatabaseLocator locator(vector<string>{
        "one",
        "two",
        "three",
        "four",
        "five"
    });
    PlatformMetadata::resetMetadataCallCount();
    // Five candidates cost exactly five calls. A walk would have cost an unknown
    // number, which is what principle I/O-19 of the specification forbids.
    locator.findFirstExistingPath();
    REQUIRE(PlatformMetadata::metadataCallCount()==5);
}
TEST_CASE("a locator returns every candidate that exists", "[agents]") {
    const AgentDatabaseLocator locator(vector<string>{".", "a path that does not exist"});
    const vector<string> allFound=locator.findAllExistingPaths();
    REQUIRE(allFound.size()==1);
    REQUIRE(allFound.front()==".");
}
TEST_CASE("two path fragments join with a separator", "[agents]") {
    // A forward slash works on all three platforms, so there is one code path rather
    // than three.
    REQUIRE(AgentRegistry::joinPath("/home/ann", ".local/share/opencode")=="/home/ann/.local/share/opencode");
    // A base that already ends in a separator does not get a second one.
    REQUIRE(AgentRegistry::joinPath("/home/ann/", ".local")=="/home/ann/.local");
    REQUIRE(AgentRegistry::joinPath("", ".local")==".local");
    REQUIRE(AgentRegistry::joinPath("/home/ann", "")=="/home/ann");
}
TEST_CASE("the OpenCode agent names its redundant event types", "[agents]") {
    const OpenCodeAgent agent;
    const AgentDescriptor descriptor=agent.describe();
    REQUIRE(descriptor.displayName=="OpenCode");
    REQUIRE(descriptor.usesSqliteDatabase);
    REQUIRE(descriptor.supportsCompaction);
    // The three types are the whole basis of the compact command, so all three must be
    // named or rows would be left behind.
    REQUIRE(descriptor.redundantEventTypes.size()==3);
    REQUIRE(descriptor.redundantEventTypes[0]=="message.updated.1");
    REQUIRE(descriptor.redundantEventTypes[1]=="message.part.updated.1");
    REQUIRE(descriptor.redundantEventTypes[2]=="session.updated.1");
    // The paths are known ahead of the file system being asked, which is what makes
    // discovery cost a fixed number of calls.
    REQUIRE_FALSE(descriptor.candidatePaths.empty());
}
TEST_CASE("the text based agents offer nothing to compact", "[agents]") {
    // These three keep one file per session with no duplicate snapshots, so the tool
    // reports their size and leaves them alone. Offering to compact them would mean
    // deleting a user's only copy of a conversation.
    const CodexAgent codexAgent;
    REQUIRE(codexAgent.describe().usesSqliteDatabase==false);
    REQUIRE(codexAgent.describe().supportsCompaction==false);
    REQUIRE(codexAgent.redundantEventTypes().empty());
    const ClaudeCodeAgent claudeAgent;
    REQUIRE(claudeAgent.describe().supportsCompaction==false);
    const AiderAgent aiderAgent;
    REQUIRE(aiderAgent.describe().supportsCompaction==false);
}
TEST_CASE("Cursor is reported but not compacted", "[agents]") {
    const CursorAgent agent;
    const AgentDescriptor descriptor=agent.describe();
    // Cursor does use SQLite, but its tables are undocumented, so compaction would mean
    // guessing. The tool reports the size and says why it will not touch it.
    REQUIRE(descriptor.usesSqliteDatabase);
    REQUIRE_FALSE(descriptor.supportsCompaction);
    REQUIRE(descriptor.redundantEventTypes.empty());
    REQUIRE(descriptor.storageDescription.find("not compact")!=string::npos);
}
TEST_CASE("Continue is reported but not compacted", "[agents]") {
    const ContinueAgent agent;
    REQUIRE_FALSE(agent.describe().supportsCompaction);
    REQUIRE(agent.redundantEventTypes().empty());
}
TEST_CASE("Cline is reported but not compacted", "[agents]") {
    const ClineAgent agent;
    const AgentDescriptor descriptor=agent.describe();
    // Cline's sessions database uses undocumented tables, so the tool reports
    // the size and refuses to compact rather than guessing.
    REQUIRE(descriptor.displayName=="Cline");
    REQUIRE(descriptor.usesSqliteDatabase);
    REQUIRE_FALSE(descriptor.supportsCompaction);
    REQUIRE(agent.redundantEventTypes().empty());
    REQUIRE_FALSE(descriptor.candidatePaths.empty());
    REQUIRE(descriptor.candidatePaths.front().find("sessions.db")!=string::npos);
}
TEST_CASE("Roo Code is reported but not compacted", "[agents]") {
    const RooCodeAgent agent;
    const AgentDescriptor descriptor=agent.describe();
    REQUIRE(descriptor.displayName=="Roo Code");
    REQUIRE_FALSE(descriptor.usesSqliteDatabase);
    REQUIRE_FALSE(descriptor.supportsCompaction);
    REQUIRE(agent.redundantEventTypes().empty());
    REQUIRE_FALSE(descriptor.candidatePaths.empty());
    // The Cline extension's storage folder must never appear here: it belongs to
    // the Cline adapter, and listing it made Cline's data report as Roo Code.
    for (const string& candidatePath : descriptor.candidatePaths) {
        REQUIRE(candidatePath.find("claude-dev")==string::npos);
    }
}
TEST_CASE("Reasonix is reported but not compacted", "[agents]") {
    const ReasonixAgent agent;
    const AgentDescriptor descriptor=agent.describe();
    REQUIRE(descriptor.displayName=="Reasonix");
    REQUIRE_FALSE(descriptor.usesSqliteDatabase);
    REQUIRE_FALSE(descriptor.supportsCompaction);
    REQUIRE(agent.redundantEventTypes().empty());
    REQUIRE_FALSE(descriptor.candidatePaths.empty());
}
TEST_CASE("VS Code is reported but not compacted", "[agents]") {
    const VscodeAgent agent;
    const AgentDescriptor descriptor=agent.describe();
    // The workspace state files hold editor state in undocumented tables.
    REQUIRE(descriptor.displayName=="VS Code");
    REQUIRE(descriptor.usesSqliteDatabase);
    REQUIRE_FALSE(descriptor.supportsCompaction);
    REQUIRE(agent.redundantEventTypes().empty());
    REQUIRE_FALSE(descriptor.candidatePaths.empty());
}
TEST_CASE("the wider catalogue is reported but never compacted", "[agents]") {
    // Fifty two more tools, none verified to keep redundant snapshots, so every
    // one is reported and left alone. Each must be findable by name and carry
    // a complete descriptor, or a report would show a blank section.
    const vector<string> expectedNames={
        string("Gemini CLI"),
        string("Windsurf"),
        string("Zed"),
        string("GitHub Copilot"),
        string("Warp"),
        string("Kilo Code"),
        string("Augment"),
        string("Trae"),
        string("PearAI"),
        string("Amp"),
        string("CommandCode"),
        string("Pi"),
        string("Void"),
        string("Tabnine"),
        string("Cody"),
        string("Codeium"),
        string("Supermaven"),
        string("Bito"),
        string("CodeGPT"),
        string("Blackbox AI"),
        string("AskCodi"),
        string("Phind"),
        string("IntelliCode"),
        string("Tabby"),
        string("Pieces"),
        string("Amazon Q"),
        string("Junie"),
        string("Qoder"),
        string("Lingma"),
        string("Comate"),
        string("Droid"),
        string("GitLab Duo"),
        string("WorkBuddy"),
        string("Kimi"),
        string("Kiro"),
        string("Qwen Code"),
        string("CodeBuddy"),
        string("MarsCode"),
        string("Qodo"),
        string("Sourcery"),
        string("Snyk"),
        string("SonarQube"),
        string("AnythingLLM"),
        string("GPT4All"),
        string("Jan"),
        string("LM Studio"),
        string("Open WebUI"),
        string("AIChat"),
        string("gptme"),
        string("LLM"),
        string("ShellGPT"),
        string("Raycast")
    };
    const AgentRegistry registry;
    for (const string& expectedName : expectedNames) {
        const optional<AgentDescriptor> descriptor=registry.findAgent(expectedName);
        REQUIRE(descriptor.has_value());
        REQUIRE_FALSE(descriptor->displayName.empty());
        REQUIRE_FALSE(descriptor->candidatePaths.empty());
        REQUIRE_FALSE(descriptor->storageDescription.empty());
        REQUIRE(descriptor->redundantEventTypes.empty());
        REQUIRE_FALSE(descriptor->supportsCompaction);
    }
}
TEST_CASE("every supported agent is listed in a fixed order", "[agents]") {
    const AgentRegistry registry;
    const vector<AgentDescriptor> allAgents=registry.allAgents();
    // Sixty two agents are supported, and the order does not change between runs, so two
    // reports can be compared line by line.
    REQUIRE(allAgents.size()==62);
    REQUIRE(allAgents[0].displayName=="OpenCode");
    REQUIRE(allAgents[1].displayName=="Cursor");
    // Every descriptor must be complete enough for a report, or a user sees a blank
    // section with no explanation.
    for (const AgentDescriptor& descriptor : allAgents) {
        REQUIRE_FALSE(descriptor.displayName.empty());
        REQUIRE_FALSE(descriptor.candidatePaths.empty());
        REQUIRE_FALSE(descriptor.storageDescription.empty());
    }
}
TEST_CASE("the filtered lists hold only the agents that qualify", "[agents]") {
    const AgentRegistry registry;
    for (const AgentDescriptor& descriptor : registry.sqliteAgents()) {
        REQUIRE(descriptor.usesSqliteDatabase);
    }
    // Only OpenCode offers compaction, and only one agent should be in that list.
    const vector<AgentDescriptor> compactable=registry.compactableAgents();
    REQUIRE(compactable.size()==1);
    REQUIRE(compactable.front().displayName=="OpenCode");
}
TEST_CASE("an agent is found by name with or without the right capitals", "[agents]") {
    const AgentRegistry registry;
    REQUIRE(registry.findAgent("OpenCode").has_value());
    // A user typing a name on a command line should not have to match capitalisation
    // to get an answer.
    REQUIRE(registry.findAgent("opencode").has_value());
    REQUIRE(registry.findAgent("OPENCODE").has_value());
    REQUIRE_FALSE(registry.findAgent("Not An Agent").has_value());
}
TEST_CASE("the supported names read as one sentence", "[agents]") {
    const AgentRegistry registry;
    const string joinedNames=registry.supportedAgentNames();
    REQUIRE(joinedNames.find("OpenCode")!=string::npos);
    REQUIRE(joinedNames.find("and")!=string::npos);
}
TEST_CASE("discovery never fails and never throws away a result", "[agents]") {
    const AgentRegistry registry;
    // Discovery runs on a machine with any combination of agents installed, so it
    // must always answer. An empty answer is valid and means nothing was installed.
    const vector<DiscoveredDatabase> discovered=registry.discoverAllDatabases();
    for (const DiscoveredDatabase& database : discovered) {
        REQUIRE_FALSE(database.agentName.empty());
        REQUIRE_FALSE(database.filePath.empty());
        REQUIRE(database.sizeBytes>=0);
    }
}
TEST_CASE("a bounded directory listing stops at its limit", "[agents]") {
    // The listing is the one place the tool opens a directory, and it is bounded so
    // that a folder with ten thousand entries costs the same as one with ten.
    const vector<string> entryNames=AgentRegistry::directoryEntryNames(".", 3);
    REQUIRE(entryNames.size()<=3);
    // A limit of zero asks for nothing, which is the caller's way of saying not to
    // look.
    REQUIRE(AgentRegistry::directoryEntryNames(".", 0).empty());
    // A folder that is not there yields nothing rather than failing.
    REQUIRE(AgentRegistry::directoryEntryNames("a folder that does not exist", 10).empty());
}
