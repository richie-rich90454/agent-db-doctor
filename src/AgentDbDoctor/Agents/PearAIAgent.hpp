// The PearAIAgent class, which knows where PearAI keeps its session state.
//
// PearAI is a VS Code fork, so it keeps session and workspace state under its
// own data dir. The layout is not a documented database, so the prune command
// does not apply. The tool still reports the size, because workspace history
// can grow quietly over time.
#ifndef AGENT_DB_DOCTOR_AGENTS_PEARAIAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_PEARAIAGENT_HPP
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
// The PearAIAgent class describes the PearAI editor.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class PearAIAgent final {
public:
    // Builds the locator with the paths PearAI uses on this platform.
    PearAIAgent();
    // The name shown to the user.
    string displayName() const;
    // The first data folder that exists, or `nullopt` when PearAI has never
    // run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. PearAI keeps
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
