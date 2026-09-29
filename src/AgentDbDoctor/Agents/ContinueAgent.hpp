// The ContinueAgent class, which knows where Continue keeps its configuration and
// indexes.
//
// Continue is a code assistant that stores a configuration folder per project and
// can keep a vector index of the codebase. Neither is a redundant snapshot
// database, so the tool reports their sizes and does not prune them.
#ifndef AGENT_DB_DOCTOR_AGENTS_CONTINUEAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_CONTINUEAGENT_HPP
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
// The ContinueAgent class describes the Continue code assistant.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class ContinueAgent final {
public:
    // Builds the locator with the folders Continue uses on this platform.
    ContinueAgent();
    // The name shown to the user.
    string displayName() const;
    // The first configuration folder that exists, or `nullopt` when Continue has
    // never run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. Continue keeps
    // no redundant snapshots, so this is always empty.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the configuration folder open.
    vector<string> likelyProcessNames() const;
    // The full description used by the registry.
    AgentDescriptor describe() const;
    // The large files inside the configuration folder, found with a bounded one
    // level listing. A vector index can reach gigabytes, so reporting it matters
    // even though nothing can be pruned.
    vector<DiscoveredDatabase> findLargeIndexFiles(int maximumFileCount, long long minimumSizeBytes) const;
private:
    // Builds the candidate list for this platform.
    static vector<string> buildCandidatePaths();
    // The locator that does the actual path testing.
    AgentDatabaseLocator databaseLocator;
};
}
#endif
