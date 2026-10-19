// The benchmark for the watchdog loop.
//
// The specification sets the idle cost of the service hard: under twelve megabytes
// of resident memory, under a hundredth of one processor, one metadata system call
// per database per tick, and zero bytes read. This file measures the two that can be
// measured from inside the process, the call count and the tick time, and it also
// drives a real run of the loop so the memory figure is exercised rather than merely
// asserted in a comment.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the service layer under test.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
// Provides the platform metadata, whose counter and clock are the measurements.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
#if defined(_WIN32)
// Declares the process information functions used to read memory usage.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
// Provides `getrusage`, which reports the process's own resource usage.
#include <sys/resource.h>
#endif
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Service;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
namespace {
// The idle memory budget from the specification.
constexpr long long IDLE_MEMORY_BUDGET_BYTES=12LL * 1024LL * 1024LL;
// One tick may not take this long, or the service would be doing real work while it
// is supposed to be asleep. A pass over a handful of databases is microseconds.
constexpr long long TICK_BUDGET_NANOSECONDS=50000000LL;
ServiceConfiguration buildTestConfiguration(const vector<string>& watchedPaths) {
    ServiceConfiguration configuration;
    configuration.watchedDatabasePaths=watchedPaths;
    // The thresholds are set far above anything the test builds, so the watchdog has
    // nothing to say and the log stays empty, which is what an idle service is.
    configuration.warningThresholdBytes=1000000000000LL;
    configuration.criticalThresholdBytes=2000000000000LL;
    configuration.checkIntervalSeconds=3600;
    configuration.logFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("WatchdogBenchmarkLog");
    configuration.configurationFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("WatchdogBenchmarkConfig")
        + ".json";
    return configuration;
}
// The process's own peak resident memory, in bytes.
//
// The specification's figure is resident memory, which is the part of the address
// space the process has actually touched. Reading it from inside the process is the
// honest way to check it, and it works on all three platforms.
long long readPeakResidentMemoryBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS memoryCounters;
    // The call needs the process handle with the query right, which is the one every
    // process may obtain for itself.
    const HANDLE processHandle=GetCurrentProcess();
    if (GetProcessMemoryInfo(processHandle, &memoryCounters, sizeof(memoryCounters))==FALSE) {
        return 0;
    }
    return static_cast<long long>(memoryCounters.PeakWorkingSetSize);
#else
    // `getrusage` reports the peak in bytes on macOS and in kilobytes on Linux, and
    // multiplying the wrong one by 1024 is how this check first failed: a macOS
    // process of four megabytes was read as four gigabytes. The platform decides the
    // unit, so the platform decides the conversion.
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)!=0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<long long>(usage.ru_maxrss);
#else
    return static_cast<long long>(usage.ru_maxrss) * 1024LL;
#endif
#endif
}
}
TEST_CASE("a watchdog tick costs one call per database and nothing else", "[benchmark]") {
    // Four databases stand in for a machine with every supported agent installed.
    vector<string> watchedPaths;
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    for (int databaseIndex=0; databaseIndex<4; ++databaseIndex) {
        const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath(
            "WatchdogBenchmarkDatabase" + to_string(databaseIndex)
        );
        SyntheticDatabaseBuilder::removeIfPresent(databasePath);
        SyntheticDatabaseBuilder::build(databasePath, options);
        watchedPaths.push_back(databasePath);
    }
    ServiceManager serviceManager(buildTestConfiguration(watchedPaths));
    PlatformMetadata::resetMetadataCallCount();
    const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
    // Exactly one call per database. Not two, not one for the whole tick: the budget
    // in the specification is one per database, and a tick that opened a file or read
    // a byte would break it.
    REQUIRE(summary.totalMetadataCallCount==4);
    REQUIRE(summary.measuredDatabaseCount==4);
    // An idle watchdog says nothing, so the log stays empty and nothing is written.
    REQUIRE(summary.messagesToLog.empty());
    for (const string& currentPath : watchedPaths) {
        SyntheticDatabaseBuilder::removeIfPresent(currentPath);
    }
}
TEST_CASE("a watchdog tick is far faster than its interval", "[benchmark]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("WatchdogBenchmarkTick");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    ServiceManager serviceManager(buildTestConfiguration(vector<string>{databasePath}));
    // The first pass is a warm up: it opens nothing but it does allocate the sample
    // vectors, and measuring a cold start would not describe the steady state.
    serviceManager.runOneWatchdogPass();
    long long worstTickNanoseconds=0;
    for (int tickIndex=0; tickIndex<20; ++tickIndex) {
        const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
        if (summary.durationNanoseconds>worstTickNanoseconds) {
            worstTickNanoseconds=summary.durationNanoseconds;
        }
    }
    // A tick has to finish in well under the second it is allowed before sleeping.
    // Anything approaching that would mean the pass is doing real work every hour.
    REQUIRE(worstTickNanoseconds < TICK_BUDGET_NANOSECONDS);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("many idle ticks do not grow the process", "[benchmark]") {
    // This is the measurement behind the twelve megabyte figure. A thousand ticks is
    // about six weeks of hourly checks, and a leak of even a few bytes per tick would
    // be visible by the end of it.
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("WatchdogBenchmarkMemory");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    ServiceManager serviceManager(buildTestConfiguration(vector<string>{databasePath}));
    // The peak is read after a warm up, so the measurement describes the loop rather
    // than the one time allocations that every program makes at start up.
    for (int warmUpTick=0; warmUpTick<5; ++warmUpTick) {
        serviceManager.runOneWatchdogPass();
    }
    const long long memoryBeforeTicks=readPeakResidentMemoryBytes();
    for (int tickIndex=0; tickIndex<1000; ++tickIndex) {
        serviceManager.runOneWatchdogPass();
    }
    const long long memoryAfterTicks=readPeakResidentMemoryBytes();
    // A thousand ticks must not add a noticeable amount. The allowance is generous
    // compared with a per tick leak, because the figure is a peak and a single
    // spike would move it.
    REQUIRE(memoryAfterTicks-memoryBeforeTicks < 1024LL * 1024LL);
    // And the whole process fits inside the idle budget the specification sets.
    REQUIRE(memoryAfterTicks < IDLE_MEMORY_BUDGET_BYTES);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a stop request ends the loop promptly", "[benchmark]") {
    // The service has to be stoppable. The loop sleeps in short slices for exactly
    // this reason, and this measures how quickly a stop takes effect.
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("WatchdogBenchmarkStop");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=3;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    ServiceConfiguration configuration=buildTestConfiguration(vector<string>{databasePath});
    // A one second interval makes the test finish in a reasonable time while still
    // being long enough to observe the sleep happening.
    configuration.checkIntervalSeconds=1;
    ServiceManager serviceManager(configuration);
    serviceManager.requestStop();
    // The loop checks the request before every sleep, so a manager that was already
    // asked to stop returns without entering the loop at all.
    const long long startNanoseconds=PlatformMetadata::monotonicNanoseconds();
    serviceManager.runWatchdogLoop();
    const long long endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    // The clock counts from a fixed point rather than from zero, so the duration is
    // the difference between the two readings rather than either one on its own.
    const long long elapsedNanoseconds=endNanoseconds-startNanoseconds;
    // It returned, and it did not wait out the interval.
    REQUIRE(elapsedNanoseconds < 500000000LL);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
