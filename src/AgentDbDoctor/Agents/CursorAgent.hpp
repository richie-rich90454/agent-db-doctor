// The CursorAgent class, which knows where Cursor keeps its workspace data.
//
// Cursor stores a SQLite database per workspace, inside a folder whose name is a
// hash of the workspace path. No fixed list of candidate paths can predict those
// names, so this agent is the reason the registry has a bounded directory listing
// method.
#ifndef AGENT_DB_DOCTOR_AGENTS_CURSORAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_CURSORAGENT_HPP
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
// The CursorAgent class describes the Cursor editor.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class CursorAgent final {
public:
    // Builds the locator with the folders Cursor uses on this platform.
    CursorAgent();
    // The name shown to the user.
    string displayName() const;
    // The first workspace storage folder that exists, or `nullopt` when Cursor has
    // never run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. Cursor writes
    // its own state, and the specification does not document its tables, so this
    // is empty and the compact command refuses to run on it.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the workspace folder open.
    vector<string> likelyProcessNames() const;
    // The full description used by the registry.
    AgentDescriptor describe() const;
    // The per workspace databases found under the storage folder, discovered with
    // a bounded one level listing.
    vector<DiscoveredDatabase> findWorkspaceDatabases(int maximumWorkspaceCount) const;
private:
    // Builds the candidate list for this platform.
    static vector<string> buildCandidatePaths();
    // The locator that does the actual path testing.
    AgentDatabaseLocator databaseLocator;
};
}
#endif
