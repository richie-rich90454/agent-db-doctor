// The AiderAgent class, which knows where Aider keeps its chat history.
//
// Aider writes a Markdown transcript into the project directory it is working in,
// named after the repository. It is small compared with a database, but it grows
// without bound and is worth reporting.
#ifndef AGENT_DB_DOCTOR_AGENTS_AIDERAGENT_HPP
#define AGENT_DB_DOCTOR_AGENTS_AIDERAGENT_HPP
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
// The AiderAgent class describes the Aider pair programming tool.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentDatabaseLocator` as a field and calls methods on it, which is the
// composition the specification requires in place of deriving from anything.
class AiderAgent final {
public:
    // Builds the locator with the file names Aider uses. Aider writes into the
    // project directory rather than into a home directory, so the candidates
    // include the current working directory.
    AiderAgent();
    // Builds a locator for one project directory, used when the user tells the
    // tool where their project is.
    explicit AiderAgent(const string& projectDirectoryPath);
    // The name shown to the user.
    string displayName() const;
    // The first history file that exists, or `nullopt` when there is none.
    optional<string> findDatabasePath() const;
    // The event type names that mark a row as a redundant snapshot. Aider keeps a
    // Markdown transcript, so this is always empty.
    vector<string> redundantEventTypes() const;
    // The programs most likely to have the history file open.
    vector<string> likelyProcessNames() const;
    // The full description used by the registry.
    AgentDescriptor describe() const;
private:
    // Builds the candidate list, given an optional project directory.
    static vector<string> buildCandidatePaths(const string& projectDirectoryPath);
    // The locator that does the actual path testing.
    AgentDatabaseLocator databaseLocator;
};
}
#endif
