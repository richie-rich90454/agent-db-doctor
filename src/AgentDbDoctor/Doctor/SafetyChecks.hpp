// The SafetyChecks class, the gatekeeper that runs before anything is changed.
//
// Every destructive operation in this tool begins by asking this class whether it
// is safe to continue. The checks are the reason a person who is out of disk
// space at midnight does not lose their work to a tool. If any check fails, the
// command stops and the database is left in exactly the state it was found in.
#ifndef AGENT_DB_DOCTOR_DOCTOR_SAFETYCHECKS_HPP
#define AGENT_DB_DOCTOR_DOCTOR_SAFETYCHECKS_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the SafetyCheckResult value type.
#include "AgentDbDoctor/Doctor/SafetyCheckResult.hpp"
// Provides the FileLockStatus value type.
#include "AgentDbDoctor/FileSystem/FileLockDetector.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
// The value types this header's interface uses live in another subsystem, so
// that namespace is brought into scope here. The directive sits after the
// includes because that is where the namespace has been declared.
using namespace AgentDbDoctor::Persistence;
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Doctor {
// The SessionSnapshot value type holds a session identifier together with what is
// known about its content.
//
// A snapshot is taken before a deletion so that the verification step can prove the
// content survived, rather than assuming it did.
struct SessionSnapshot final {
    // The session identifier, for example `ses_abc123`.
    string sessionIdentifier;
    // The number of readable text parts the session held when the snapshot was
    // taken.
    long long readableTextPartCount=0;
    // The number of messages the session held.
    long long messageCount=0;
    // The number of parts the session held, of any kind.
    long long partCount=0;
    // True when the content lives outside the event table, which is the condition
    // that makes deleting event rows safe.
    bool contentPersistsOutsideEventTable=false;
};
// The SafetyChecks class runs the pre-flight validation for a destructive
// operation.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so it is effectively a namespace of checks and every method is `static`.
class SafetyChecks final {
public:
    SafetyChecks()=delete;
    // Verifies that message content survives outside the event table.
    //
    // This is the most important check in the tool. The compact command deletes rows
    // from the event table, and if the conversation text lived only in those rows,
    // deleting them would destroy the user's history. So this check opens the
    // oldest session, counts how many readable text parts it has, and confirms
    // that the parts are stored in the `part` table rather than only in events.
    static SafetyCheckResult verifyMessageContentPersistsOutsideEventTable(Database& database);
    // Verifies that the oldest session still has readable content, which is the
    // direct evidence that a previous compaction did not destroy anything.
    static SafetyCheckResult verifyOldestSessionStillHasReadableContent(Database& database);
    // Verifies that no other program has the database open. A database in use can
    // be modified underneath the tool, and every guarantee the tool makes would
    // become a lie.
    static SafetyCheckResult verifyDatabaseIsNotLocked(const string& databaseFilePath);
    // Verifies that the volume holding the database has room for a safety copy.
    static SafetyCheckResult verifySufficientFreeSpaceForBackup(
        const string& databaseFilePath,
        long long currentFileSizeBytes
    );
    // Verifies that the write ahead log and the rollback journal are not both
    // present, which would mean the database was left in an interrupted state.
    static SafetyCheckResult verifyNoInterruptedWriteIsPending(const string& databaseFilePath);
    // Verifies that the database passed its quick integrity check, because
    // deleting rows from a damaged database makes the damage worse.
    static SafetyCheckResult verifyDatabaseIsNotDamaged(Database& database);
    // Verifies that the agent's schema has the columns the compaction query needs. An
    // older release of an agent may store a different shape, and querying a column
    // that does not exist is a programming error rather than a user error.
    static SafetyCheckResult verifySchemaSupportsCompaction(Database& database);
    // Runs every check above and returns them all, in a fixed order, so the report
    // always reads the same way. The caller decides whether a single failure stops
    // the operation, which it always does.
    static vector<SafetyCheckResult> runAllPreFlightChecks(
        Database& database,
        const string& databaseFilePath
    );
    // Captures what the oldest session looks like, so the verification step after
    // a deletion can compare against it. Returns `nullopt` when the database has
    // no sessions at all, which is not an error.
    static vector<SessionSnapshot> captureOldestSessions(Database& database, int sessionCountToCapture);
    // Re-reads the sessions captured before a deletion and confirms that every one
    // of them still holds the same readable content. This is the proof that a
    // compaction did not lose anything, and it is checked after the deletion rather
    // than assumed before it.
    static SafetyCheckResult verifySnapshotsSurvivedUnchanged(
        Database& database,
        const vector<SessionSnapshot>& snapshotsTakenBefore
    );
    // Forces the survival check to fail. Used only by the test suite, which cannot
    // otherwise exercise the restore path on a healthy database. The flag lives
    // here rather than in the compactor so a test can trip it without changing how
    // the compactor is built.
    static bool& failSurvivalCheckForTests();
    // Counts the readable text parts of one session. A readable part is one whose
    // stored text is longer than twenty characters, because anything shorter is
    // more likely to be a placeholder than a message. Returns zero when the query
    // fails, which makes a failure look like "no content" and therefore makes the
    // surrounding check fail closed.
    static long long countReadableTextParts(Database& database, const string& sessionIdentifier);
private:
};
}
#endif
