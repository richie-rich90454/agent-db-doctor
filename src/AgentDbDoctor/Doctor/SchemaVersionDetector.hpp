// The SchemaVersionDetector class, which names the generation of an OpenCode
// database from what is actually inside it.
//
// OpenCode writes two generations of history. The first generation is the
// `event` rows with a `.1` suffix, one per superseded message state. The
// second generation is the `session.next.*` event family together with the
// `session_message`, `session_input`, and `session_context_epoch` tables. Both
// generations can be present at once, because newer releases dual write them.
// There is deliberately no version number anywhere: the file's `user_version`
// pragma is zero on real databases, so the content is the only signal.
#ifndef AGENT_DB_DOCTOR_DOCTOR_SCHEMAVERSIONDETECTOR_HPP
#define AGENT_DB_DOCTOR_DOCTOR_SCHEMAVERSIONDETECTOR_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Database wrapper the probes run against.
#include "AgentDbDoctor/Persistence/Database.hpp"
// The namespace the interface uses lives in another subsystem, so it is
// brought into scope here. The directive sits after the includes because that
// is where the namespace has been declared.
using namespace AgentDbDoctor::Persistence;
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Doctor {
// The generation of an OpenCode database, as read from its rows.
//
// `Unknown` never means "empty". An empty event table on a well formed schema
// is `Version1`: a fresh database simply has nothing in it yet. `Unknown`
// means rows exist that belong to neither generation, which is how a future
// third generation first appears.
enum class SchemaGeneration {
    Unknown,
    Version1,
    Version1And2,
    Version2Only
};
// The SchemaVersionDetector class reads the generation from a database.
//
// The class is `final` because the specification forbids inheritance. It holds
// no state, so every method is `static`. Every probe stops at the first
// matching row, so detection costs four constant time queries and never scans
// a table.
class SchemaVersionDetector final {
public:
    SchemaVersionDetector()=delete;
    // Names the generation of the database. Never throws: a probe that fails
    // reports "not found" rather than aborting detection, because a missing
    // table is itself an answer.
    static SchemaGeneration detect(Database& database);
    // A plain language description of a generation, for reports. The empty
    // generation is described by the caller, never here.
    static string describe(SchemaGeneration generation);
private:
};
}
#endif
