// The ClaudeCodeAgent class, which knows where Claude Code keeps its sessions.
//
// Claude Code stores each conversation as a JSONL file, one JSON object per line,
// under a folder named after the project it was working on. Like the Codex CLI it
// writes no duplicate snapshots, so the tool reports the size and does not compact.
#ifndef AGENT_DB_DOCTOR_AGENTS_CLAUDECODEAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_CLAUDECODEAGENT_HPP
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AgentDatabaseLocator and the AgentDescriptor.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Agents {
// The ClaudeCodeAgent class describes the Anthropic Claude Code agent.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class ClaudeCodeAgent final {
public:
    // Builds the locator with the paths Claude Code uses on this platform.
    ClaudeCodeAgent();
    // The name shown to the user.
    string displayName() const;
    // The first project folder that exists, or `nullopt` when Claude Code has
    // never run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. Claude Code
    // keeps no database, so this is always empty.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the session folder open.
    vector<string> likelyProcessNames() const;
    // The full description used by the registry.
    AgentDescriptor describe() const;
    // The files that hold conversation history, found by a bounded one level
    // listing of the project folder. Returns an empty vector when the folder is
    // missing.
    vector<string> findSessionFiles(int maximumFileCount) const;
private:
    // Builds the candidate list for this platform.
    static vector<string> buildCandidatePaths();
    // The locator that does the actual path testing.
    AgentDatabaseLocator databaseLocator;
};
}
#endif
