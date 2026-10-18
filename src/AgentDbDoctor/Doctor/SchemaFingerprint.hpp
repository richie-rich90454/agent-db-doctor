// The SchemaFingerprint class, which notices when an agent changes the layout of
// its database.
//
// Every adapter in this tool was written against the layout an agent had on the day
// it was written. An agent release that renames a column or adds a table can make
// the tool quietly less useful: the queries still run, but they answer about the
// wrong things. The fingerprint is the cheap defence. The watchdog writes one
// fingerprint per database into a small file once an hour, and a later diagnose
// compares the fingerprint it computes with what was written. A difference is
// reported as a warning, which is the signal that the adapter needs a look.
//
// The fingerprint is deliberately a fingerprint of the schema, not of the data: the
// rows change every time an agent is used, and that is normal.
#ifndef AGENT_DB_DOCTOR_DOCTOR_SCHEMAFINGERPRINT_HPP
#define AGENT_DB_DOCTOR_DOCTOR_SCHEMAFINGERPRINT_HPP
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Database wrapper the fingerprint is computed from.
#include "AgentDbDoctor/Persistence/Database.hpp"
using namespace std;
using namespace AgentDbDoctor::Persistence;
namespace AgentDbDoctor::Doctor {
// The SchemaFingerprint class computes and remembers the shape of a database.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class SchemaFingerprint final {
public:
    SchemaFingerprint()=delete;
    // Computes the fingerprint of the schema the connection sees. The value is a
    // hash of every table and index definition, so a renamed column or an added
    // table changes it and ordinary data changes do not.
    static string fingerprintOf(Database& database);
    // The fingerprint written by the last watchdog run, or nothing when the file
    // does not exist, cannot be read, or has no entry for this database. A missing
    // entry is not an error: it only means there is nothing to compare against.
    static optional<string> storedFingerprintFor(const string& databaseFilePath);
    // Writes the fingerprint into the store, replacing the stored value for that
    // database. Used by the watchdog, which runs with the rights to write the store.
    // Returns false when the store cannot be written, which is reported rather than
    // thrown: a fingerprint is a convenience, not a guarantee.
    static bool storeFingerprint(const string& databaseFilePath, const string& fingerprint);
    // Overrides the store path. Used only by the tests, which cannot write to the
    // shared location the service uses.
    static void setStorePathForTests(const string& storeFilePath);
private:
    // The path of the store: a small tab separated file beside the configuration.
    static string storeFilePath();
};
}
#endif
