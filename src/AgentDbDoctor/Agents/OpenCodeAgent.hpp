// The OpenCodeAgent class, which knows where OpenCode keeps its database.
//
// OpenCode stores its session history in a single SQLite database whose `event`
// table receives a full snapshot of the conversation on every change. The
// snapshot rows carry a version suffix, which is what makes it possible to delete
// the old ones without losing anything.
#ifndef AGENT_DB_DOCTOR_AGENTS_OPENCODEAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_OPENCODEAGENT_HPP
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
// The OpenCodeAgent class describes one supported coding agent.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class OpenCodeAgent final {
public:
    // Builds the locator with the paths OpenCode uses on this platform. The paths
    // are decided once, at construction, so that later calls cost nothing.
    OpenCodeAgent();
    // The name shown to the user.
    string displayName() const;
    // The first path that exists, or `nullopt` when OpenCode has never run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. A trailing
    // `.1` is OpenCode's convention for "the first version of this object", and
    // the unversioned row is the live one.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the database open.
    vector<string> likelyProcessNames() const;
    // The full description used by the registry.
    AgentDescriptor describe() const;
private:
    // Builds the candidate list for this platform. It is a static helper because
    // the answer depends only on which operating system is running, not on any
    // object state.
    static vector<string> buildCandidatePaths();
    // The event type names, held as a field so that a report renders them without
    // building a new vector every time.
    vector<string> redundantTypeNames;
    // The locator that does the actual path testing.
    AgentDatabaseLocator databaseLocator;
};
}
#endif
