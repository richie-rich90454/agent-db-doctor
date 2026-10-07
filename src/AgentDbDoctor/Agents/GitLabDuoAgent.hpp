// The GitLabDuoAgent class, which knows where GitLab Duo keeps its data.
//
// GitLab Duo stores assistant state in editor extension storage.
// There is no session database to scan, so the prune command does not apply.
// The tool still reports the folder size, because extension state can
// quietly accumulate over time.
#ifndef AGENT_DB_DOCTOR_AGENTS_GITLABDUOAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_GITLABDUOAGENT_HPP
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
// The GitLabDuoAgent class describes the GitLab Duo agent.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class GitLabDuoAgent final {
public:
    // Builds the locator with the folder GitLab Duo uses on this platform.
    GitLabDuoAgent();
    // The name shown to the user.
    string displayName() const;
    // The first data folder that exists, or `nullopt` when GitLab Duo has never
    // run here.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. GitLab Duo
    // keeps no database, so this is always empty.
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
