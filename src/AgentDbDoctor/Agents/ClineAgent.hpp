// The ClineAgent class, which knows where the Cline extension keeps its sessions.
//
// Cline keeps its sessions in a SQLite database with undocumented tables. There
// is no documented snapshot duplication to remove, so the prune command does
// not apply. The tool still reports the size, because the sessions database
// grows with every task.
#ifndef AGENT_DB_DOCTOR_AGENTS_CLINEAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_CLINEAGENT_HPP
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
// The ClineAgent class describes the Cline VS Code extension.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class ClineAgent final {
public:
    // Builds the locator with the paths the Cline extension uses on this platform.
    ClineAgent();
    // The name shown to the user.
    string displayName() const;
    // The sessions database path when it exists, or `nullopt` when Cline has
    // never run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. The Cline
    // tables are undocumented, so this is always empty.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the sessions database open.
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
