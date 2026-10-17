// The TraeAgent class, which knows where Trae keeps its session state.
//
// Trae is a VS Code fork, so it keeps session and workspace state under its
// own data dir. The layout is not a documented database, so the compact command
// does not apply. The tool still reports the size, because workspace history
// can grow quietly over time.
#ifndef AGENT_DB_DOCTOR_AGENTS_TRAEAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_TRAEAGENT_HPP
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
// The TraeAgent class describes the Trae editor.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class TraeAgent final {
public:
    // Builds the locator with the paths Trae uses on this platform.
    TraeAgent();
    // The name shown to the user.
    string displayName() const;
    // The first data folder that exists, or `nullopt` when Trae has never
    // run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. Trae keeps
    // no documented database, so this is always empty.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the data folder open.
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
