// The CodexAgent class, which knows where the Codex CLI keeps its sessions.
//
// The Codex CLI stores each session as a plain text file where every line is one
// JSON object. There is no snapshot duplication to remove, so the compact command
// does not apply. The tool still reports the size, because a year of sessions
// can quietly occupy tens of gigabytes of plain text.
#ifndef AGENT_DB_DOCTOR_AGENTS_CODEXAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_CODEXAGENT_HPP
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
// The CodexAgent class describes the OpenAI Codex command line agent.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class CodexAgent final {
public:
    // Builds the locator with the paths the Codex CLI uses on this platform.
    CodexAgent();
    // The name shown to the user.
    string displayName() const;
    // The first session folder that exists, or `nullopt` when the CLI has never
    // run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. The Codex CLI
    // keeps no database, so this is always empty.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the session folder open.
    vector<string> likelyProcessNames() const;
    // The full description used by the registry.
    AgentDescriptor describe() const;
private:
    // Builds the candidate list for this platform.
    static vector<string> buildCandidatePaths();
    // The locator that does the actual path testing.
    AgentDatabaseLocator databaseLocator;
};
}
#endif
