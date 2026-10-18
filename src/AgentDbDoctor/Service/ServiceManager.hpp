// The ServiceManager class, which installs, controls, and runs the system service.
//
// The service is what turns this tool from something a person remembers to run
// into something that notices on its own. The specification requires it to keep
// under twelve megabytes of resident memory while idle, to use one thread, and to
// cost exactly one metadata system call per database per tick. Everything in this
// class is arranged around those three numbers.
#ifndef AGENT_DB_DOCTOR_SERVICE_SERVICEMANAGER_HPP
#define AGENT_DB_DOCTOR_SERVICE_SERVICEMANAGER_HPP
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ServiceConfiguration value type.
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
// Provides the ServiceStatusSnapshot value type, which the console report prints.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Service {
// The WatchdogSample value type is one tick's result for one database.
//
// It is a small value returned by value, and its fields are ordered from widest to
// narrowest so that the compiler adds no padding between them.
struct WatchdogSample final {
    // The full path that was measured.
    string databaseFilePath;
    // The logical size in bytes as of this tick.
    long long currentSizeBytes=0;
    // The size at the previous tick, or zero on the first one.
    long long previousSizeBytes=0;
    // True when the database is above the warning threshold.
    bool isAboveWarningThreshold=false;
    // True when the database is above the critical threshold.
    bool isAboveCriticalThreshold=false;
    // True when the database has grown since the previous tick.
    bool hasGrown=false;
    // The number of metadata system calls this tick cost. It is one per database,
    // which is the number the performance contract allows.
    int metadataCallCount=0;
};
// The WatchdogRunSummary value type is what one full pass over every watched
// database produced.
struct WatchdogRunSummary final {
    // How many databases were measured.
    int measuredDatabaseCount=0;
    // How many are above the warning threshold.
    int warningCount=0;
    // How many are above the critical threshold.
    int criticalCount=0;
    // How many grew since the previous tick.
    int grownCount=0;
    // The total metadata system calls the pass cost. The performance contract
    // allows fewer than twenty, and the benchmark asserts that number.
    int totalMetadataCallCount=0;
    // The wall clock time the pass took, in nanoseconds.
    long long durationNanoseconds=0;
    // The samples themselves, one per watched database.
    vector<WatchdogSample> samples;
    // One line per database that needs attention, empty when nothing does. The
    // service writes these to its log, which is why it is allowed to build them.
    vector<string> messagesToLog;
};
// The ServiceManager class owns the platform specific parts of the service.
//
// The class is `final` because the specification forbids inheritance. The platform
// differences are handled by three implementation files, one per operating system,
// so the calling code never contains a conditional.
class ServiceManager final {
public:
    // Builds a manager for one configuration. The configuration is held by value,
    // because the service reads it once and never changes it while running.
    explicit ServiceManager(ServiceConfiguration configuration);
    // The name the operating system knows the service by. It is the same on every
    // platform, so a script written on one works on another.
    static string serviceDisplayName();
    // The full path of the running executable, which is what the service runs. It
    // is taken from the current process rather than from the search path, so a
    // service always runs the exact version it was installed from.
    static string currentExecutablePath();
    // Registers the service with the operating system and writes a default
    // configuration file. Needs administrator rights and says so plainly when they
    // are missing. Throws `ServiceOperationException` with a sentence explaining
    // what to do when anything goes wrong.
    void install();
    // Removes the service from the operating system. The configuration file is left
    // in place, because deleting a person's settings during an uninstall is never
    // what they meant.
    void uninstall();
    // Starts the service if it is installed and not already running.
    void start();
    // Stops the service.
    void stop();
    // Reads the current state into `statusOut`, so the caller can print it.
    ServiceStatusSnapshot readStatus();
    // The loop the service process runs. It never returns under normal operation.
    //
    // Each pass costs one metadata call per database, decides from the previous
    // pass's numbers whether anything is worth saying, and then sleeps. The sleep is
    // the only thing between passes, which is what keeps the idle cost at well under
    // a hundredth of one processor.
    void runWatchdogLoop();
    // Runs one pass over every watched database and returns what it found. It is
    // public so the benchmark suite can drive it directly instead of waiting for
    // the real loop.
    WatchdogRunSummary runOneWatchdogPass();
    // The configuration this manager was built with.
    const ServiceConfiguration& configuration() const;
    // Asks the watchdog loop to stop at its next opportunity. The loop checks
    // this between short sleeps, so a stop takes effect within a fraction of a
    // second rather than at the end of the configured interval.
    void requestStop();
private:
    // The paths the watchdog measures, worked out from the configuration. An empty
    // watched list in the configuration means every supported agent, which is
    // resolved here once rather than on every tick.
    vector<string> resolveWatchedDatabasePaths() const;
    // Writes the platform specific service definition. One implementation per
    // operating system.
    void writePlatformServiceDefinition() const;
    // Removes the platform specific service definition.
    void removePlatformServiceDefinition() const;
    // Asks the platform to start, stop, or query the service. One implementation
    // per operating system.
    void startPlatformService() const;
    void stopPlatformService() const;
    ServiceStatusSnapshot readPlatformStatus() const;
    // The configuration, held by value.
    ServiceConfiguration serviceConfiguration;
    // The sizes seen at the previous tick, keyed by path. This is what lets a tick
    // cost one metadata call and nothing else: growth is worked out from the
    // previous number rather than by opening the file.
    map<string, long long> previousSizes;
    // True when the loop has been asked to stop. It is a plain flag rather than an
    // atomic because the specification allows exactly one thread.
    bool stopRequested=false;
    // The watched paths discovered when the configuration named none. Kept between
    // ticks so the whole discovery does not re-run every hour, and refreshed on the
    // schedule in the implementation so a database an agent creates later is picked
    // up within that hour.
    mutable vector<string> cachedDiscoveredPaths;
    // When the cache above was last refreshed, on the monotonic clock.
    mutable long long cachedPathsRefreshedNanoseconds=0;
};
}
#endif
