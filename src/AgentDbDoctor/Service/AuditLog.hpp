// The AuditLog class, the record of what the tool changed by itself.
//
// The watchdog can compact and vacuum a database on its own, and a change made
// while nobody is watching must still be findable afterwards. The log is that
// record: one line per step the automatic maintenance performed, and one line per
// step it refused or failed, each carrying the path, the size before and after,
// the safety copy that was involved, and the exit code of the step.
//
// The store is a tab separated text file beside the service configuration. A
// field never contains a tab or a newline, because the writer turns both into a
// space before the line is written: a value that could break the format would
// make every later line unreadable, and a log that cannot be read is worse than a
// log with a space in one sentence. Lines are pruned on every write, so the file
// cannot grow without bound.
//
// The write goes through a temporary file and a rename, exactly like the history
// store, so a reader never sees half a log and a crash mid-write leaves the
// previous file intact.
#ifndef AGENT_DB_DOCTOR_SERVICE_AUDITLOG_HPP
#define AGENT_DB_DOCTOR_SERVICE_AUDITLOG_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
using namespace std;
namespace AgentDbDoctor::Service {
// One recorded action.
//
// The fields are public and the type is a plain value, matching the history
// store's point type: it is produced by the maintenance, written by this class,
// and read back by the audit command.
struct AuditEntry final {
    // When the action happened, as seconds since the Unix epoch.
    long long epochSeconds=0;
    // What was done. The values used today are `automatic-compaction` and
    // `automatic-vacuum`.
    string action;
    // The database the action was about.
    string databaseFilePath;
    // The size of the database in bytes before the step ran.
    long long sizeBeforeBytes=0;
    // The size of the database in bytes after the step ran. Equal to the size
    // before when the step changed nothing.
    long long sizeAfterBytes=0;
    // The safety copy the step made, or the empty string when there was none.
    // The copy is normally removed again after a successful run, so this records
    // where it was rather than where it is.
    string backupFilePath;
    // A sentence describing the outcome, in the tool's own words.
    string outcome;
    // The exit code the step ended with. Zero means it finished, and one means it
    // was refused or failed.
    int exitCode=0;
};
// The AuditLog class records and reads what the tool did by itself.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class AuditLog final {
public:
    AuditLog()=delete;
    // The path of the store: a small text file beside the service configuration.
    static string storeFilePath();
    // Overrides the store path. Used only by the tests, which cannot write to the
    // shared location the service uses.
    static void setStorePathForTests(const string& storeFilePath);
    // Appends one entry, pruning entries older than the retention window and
    // capping the file at the maximum line count. Returns false when the store
    // cannot be written, which the caller records as a failure rather than
    // throwing: a service must not die because its own log is read only.
    static bool append(const AuditEntry& entry);
    // Every entry the store holds, oldest first, so the newest entry is
    // `readAll().back()`. A line that is not shaped like an entry is skipped
    // rather than failing the whole read: a damaged log should still show
    // everything that can still be read.
    static vector<AuditEntry> readAll();
    // How many days the store keeps. Older entries are pruned on the next write.
    // Half a year is long enough that a change is still findable when somebody
    // wonders about it, and short enough that the file stays small.
    static constexpr int RETENTION_DAYS=180;
    // The most lines the store is allowed to hold.
    static constexpr size_t MAXIMUM_LINE_COUNT=20000;
};
}
#endif
