// The AgentDatabaseDoctor class, which runs one diagnostic pass over one database.
//
// This is the class the `diagnose` command and the system service both call. It
// is written so that a full pass costs fewer than twenty metadata system calls
// and reads no more than one hundred bytes from the database itself, which is the
// performance contract in the specification.
#ifndef AGENT_DB_DOCTOR_DOCTOR_AGENTDATABASEDOCTOR_HPP
#define AGENT_DB_DOCTOR_DOCTOR_AGENTDATABASEDOCTOR_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>

// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DiagnosisReport value type.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
// Provides the AgentRegistry and the DiscoveredDatabase value type.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
#include "AgentDbDoctor/Persistence/Database.hpp"
// The value types this header's interface uses live in another subsystem, so
// that namespace is brought into scope here. The directive sits after the
// includes because that is where the namespace has been declared.
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::Persistence;

// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Doctor {
// The AgentDatabaseDoctor class produces diagnosis reports.
//
// The class is `final` because the specification forbids inheritance. It holds an
// `AgentRegistry` by value and asks it where the databases are, rather than
// deriving from anything.
class AgentDatabaseDoctor final {
public:
    // Builds a doctor with its own registry of supported agents.
    AgentDatabaseDoctor();
    // Runs a full diagnostic pass over the database at `databaseFilePath`.
    //
    // The pass never writes to the database and never modifies it in any way. It
    // reads one hundred bytes of header, asks the file system for the sizes, and
    // then runs a handful of constant time queries. That is why it can complete
    // in well under five hundred milliseconds on a database of any size.
    //
    // `agentName` is used only for the report heading. Pass the empty string when
    // the agent is not known.
    DiagnosisReport diagnoseDatabase(const string& databaseFilePath, const string& agentName);
    // Runs a full diagnostic pass over every database every supported agent
    // reports. Databases that are not present are not reported, because a user
    // who has never installed an agent does not need to hear about it.
    vector<DiagnosisReport> diagnoseAllDiscoveredDatabases();
    // Runs the full pass only when the file's metadata suggests it is worth
    // looking at. This is the method the watchdog uses on every tick.
    //
    // It performs exactly one metadata call, and returns nothing when the database
    // is below `warningThresholdBytes` and has not grown since the previous tick.
    // The caller keeps the previous size and passes it in, which is what lets the
    // whole tick cost one system call per database.
    optional<DiagnosisReport> diagnoseIfAboveThreshold(
        const string& databaseFilePath,
        const string& agentName,
        long long warningThresholdBytes,
        long long previousSizeBytes
    );
private:
    // Runs the quick integrity check and records the result on the report.
    //
    // `enableIntegrityCheck` is false on the watchdog path, because a full page
    // check reads the entire file and would defeat the purpose of a one call tick.
    void recordIntegrityStatus(const string& databaseFilePath, DiagnosisReport& report, bool enableIntegrityCheck);
    // Fills in the sizes the file system reports, including the side files.
    void recordStorageMetrics(const string& databaseFilePath, DiagnosisReport& report);
    // Fills in the counts, using the constant time statistics table where it can
    // and falling back to a full scan only for the small tables.
    void recordTableRowCounts(Database& database, DiagnosisReport& report);
    // Counts the event rows that are known duplicates, one indexed lookup per
    // event type. The number of types is small and fixed, which is what makes this
    // constant time.
    void recordRedundantEventCount(Database& database, DiagnosisReport& report);
    // The registry that knows where the databases are.
    AgentRegistry agentRegistry;
};
}
#endif
