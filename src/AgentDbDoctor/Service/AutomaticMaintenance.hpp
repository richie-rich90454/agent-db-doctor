// The AutomaticMaintenance class, the watchdog's hand that can clean up while
// nobody is watching.
//
// Everything else in the service only reports. This class is the one part that
// changes a file, and it is written so that a refusal is as visible as a success:
// a database above its urgent threshold that was left alone appears in the log
// with the reason, never as silence. The rules, in the order they are applied to
// a database whose size the watchdog has just measured:
//
// - The database must be above its critical threshold: the threshold of a policy
//   that names it, or the configuration's own critical threshold.
// - Automatic maintenance must be allowed: the policy's flag when a policy names
//   the database, otherwise the configuration's `allowAutomaticCompaction`. The
//   vacuum falls under the same permission as the compaction, because both rewrite
//   hundreds of gigabytes when they are wrong. The service applies this permission
//   before it offers a database at all, so a refusal here only happens when this
//   class is driven directly; the check is kept because a caller must not be able
//   to skip a rule by calling the class instead of the service.
// - The database must look idle: its modification time must be at least fifteen
//   minutes old. An agent that wrote a second ago is in use, and a file that is
//   being written is the one file the tool must not touch.
// - The database must not be locked by another program.
//
// When all four hold, the compaction runs first and the vacuum second, both
// quietly and pre-answered, each keeping its own safety copy and removing it
// again when its checks pass. Every step and every outcome is appended to the
// audit log. Nothing in this class throws: the watchdog has nobody to catch for
// it, so every failure is turned into a log line and an audit entry instead.
#ifndef AGENT_DB_DOCTOR_SERVICE_AUTOMATICMAINTENANCE_HPP
#define AGENT_DB_DOCTOR_SERVICE_AUTOMATICMAINTENANCE_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the ServiceConfiguration value type, which carries the thresholds and
// the per database policies the rules above read.
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
// Provides the ConsoleReport class, which the two commands print through.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Service {
// The AutomaticMaintenance class performs the cleanup the service may do on its
// own.
//
// The class is `final` because the specification forbids inheritance. It holds
// the configuration and the reporter by reference, because the configuration is
// read once at start up and the reporter belongs to the caller.
class AutomaticMaintenance final {
public:
    // The outcome of one attempt, whether it did anything or not.
    struct MaintenanceRunSummary final {
        // True when every rule passed and the steps were attempted. A run that
        // was refused before that point leaves this false.
        bool ran=false;
        // The database this attempt was about.
        string databaseFilePath;
        // The bytes the file shrank by, which is zero when nothing was freed.
        long long bytesFreed=0;
        // The number of rows the compaction removed.
        long long compactedRows=0;
        // True when the compaction removed rows.
        bool compactRan=false;
        // True when the vacuum replaced the file with a smaller one.
        bool vacuumRan=false;
        // One line for the watchdog's log, saying what happened or why nothing
        // could. Always set: a refusal with no line would be the silence this
        // class exists to avoid.
        string logLine;
    };
    // Builds the maintenance for one configuration. Both arguments are held by
    // reference and must outlive the object.
    AutomaticMaintenance(const ServiceConfiguration& configuration, const ConsoleReport& reporter);
    // Applies the rules above to one measured database and returns what happened.
    // The database must already be at or above its critical threshold; this method
    // checks again rather than trusting the caller, because a caller that skipped
    // its own gate must not be able to skip this one too.
    MaintenanceRunSummary runForIfRequired(const string& databaseFilePath, long long currentSizeBytes);
    // Overrides how many minutes of quiet a database needs. Used only by the
    // tests, which cannot wait fifteen minutes to watch a real run. A negative
    // value restores the default, so a test can leave the process as it found it.
    static void setIdleMinutesForTests(int minutes);
    // How many minutes a database must have gone unwritten before the maintenance
    // will touch it. Fifteen minutes is longer than any pause between two prompts
    // and far shorter than an hour, which is the tick interval.
    static constexpr int AUTO_MAINTENANCE_IDLE_MINUTES=15;
private:
    // Appends one entry to the audit log. The write is best effort, exactly as
    // the audit log itself is: a failure to record an action must not become a
    // failure of the action.
    static void appendAuditEntry(
        const string& action,
        const string& databaseFilePath,
        long long sizeBeforeBytes,
        long long sizeAfterBytes,
        const string& backupFilePath,
        const string& outcome,
        int exitCode
    );
    const ServiceConfiguration& serviceConfiguration;
    const ConsoleReport& reporter;
};
}
#endif
