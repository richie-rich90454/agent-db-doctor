// The shared implementation of the ServiceManager class.
//
// Everything in this file is platform independent. The three functions that cannot
// be are declared in the header and defined once each in
// ServiceManagerWindows.cpp, ServiceManagerLinux.cpp, and ServiceManagerMacOS.cpp,
// so the calling code never contains an operating system conditional.
// Provides `map`, the ordered dictionary the previous sizes are kept in.
#include <map>
// Provides `ifstream` and `getline`, which read the last line of the log.
#include <fstream>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ServiceManager class declaration.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
// Provides the AgentRegistry, used to discover the databases when none are listed.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// Provides PlatformMetadata, for the clock, the sleep, and the metadata calls.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the Logger class, which writes the one line an hour the service logs.
#include "AgentDbDoctor/Common/Logger.hpp"
// Provides the Logger and the exception types.
#include "AgentDbDoctor/Common/Exceptions.hpp"
// Provides the number to text conversions used in the log lines.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
// Provides the Database wrapper, used to fingerprint each watched database once an
// hour.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the schema fingerprint written for each watched database.
#include "AgentDbDoctor/Doctor/SchemaFingerprint.hpp"
// Provides the growth record the watchdog appends to once an hour.
#include "AgentDbDoctor/Service/HistoryStore.hpp"
// Provides `time`, used for the timestamp of each recorded size.
#include <ctime>
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Service {
// The name the operating system knows the service by. It is a constant so that the
// three platform implementations and the status message cannot disagree about it.
static constexpr const char* SERVICE_NAME="agent-db-doctor";
// The length of one sleep between stop checks. It is short enough to feel immediate
// and long enough not to spin.
static constexpr int SLEEP_SLICE_MILLISECONDS=200;
ServiceManager::ServiceManager(ServiceConfiguration configuration)
    : serviceConfiguration(std::move(configuration)) {
}
const ServiceConfiguration& ServiceManager::configuration() const {
    return serviceConfiguration;
}
void ServiceManager::requestStop() {
    stopRequested=true;
}
string ServiceManager::serviceDisplayName() {
    return string(SERVICE_NAME);
}
// How often the discovered path list is refreshed when the configuration names no
// paths. An hour matches the tick interval: a database an agent creates is noticed
// within one interval, and the discovery's metadata calls happen once an hour
// rather than on every tick.
static constexpr long long WATCHED_PATHS_REFRESH_NANOSECONDS=3600LL*1000000000LL;
vector<string> ServiceManager::resolveWatchedDatabasePaths() const {
    if (!serviceConfiguration.watchedDatabasePaths.empty()) {
        // The user named the files, so exactly those are watched.
        return serviceConfiguration.watchedDatabasePaths;
    }
    // Nothing was named, so every supported agent is asked where its data is. The
    // answer is cached, because re-running the whole discovery on every tick cost
    // hundreds of metadata calls an hour that the performance contract does not
    // allow. The cache is refreshed once an hour so new databases are still found.
    const long long nowNanoseconds=PlatformMetadata::monotonicNanoseconds();
    const bool cacheNeedsRefresh=cachedDiscoveredPaths.empty()
        || nowNanoseconds-cachedPathsRefreshedNanoseconds>=WATCHED_PATHS_REFRESH_NANOSECONDS;
    if (cacheNeedsRefresh) {
        vector<string> refreshedPaths;
        AgentRegistry registry;
        for (const DiscoveredDatabase& discovered : registry.discoverAllDatabases()) {
            refreshedPaths.push_back(discovered.filePath);
        }
        cachedDiscoveredPaths=std::move(refreshedPaths);
        cachedPathsRefreshedNanoseconds=nowNanoseconds;
    }
    return cachedDiscoveredPaths;
}
WatchdogRunSummary ServiceManager::runOneWatchdogPass() {
    const long long startNanoseconds=PlatformMetadata::monotonicNanoseconds();
    WatchdogRunSummary summary;
    // The counter is read before the paths are resolved, so that the discovery calls
    // made on a refresh tick are part of the pass's honest total rather than being
    // measured away. On every other tick the resolution costs nothing, because the
    // list is cached.
    const long long callsBeforePass=PlatformMetadata::metadataCallCount();
    const vector<string> watchedPaths=resolveWatchedDatabasePaths();
    for (const string& currentPath : watchedPaths) {
        // This is the whole tick: one metadata call, no file opened, no bytes read.
        // Everything else in this method is arithmetic on the number it returned and
        // on the number the previous tick returned.
        const FileMetadata currentStatus=PlatformMetadata::readFileMetadata(currentPath);
        WatchdogSample sample;
        sample.databaseFilePath=currentPath;
        sample.currentSizeBytes=currentStatus.logicalSizeBytes;
        // The `find` on the map returns an iterator, and comparing it with `end`
        // tests presence without inserting a default.
        const auto previousEntry=previousSizes.find(currentPath);
        if (previousEntry!=previousSizes.end()) {
            sample.previousSizeBytes=previousEntry->second;
        }
        sample.metadataCallCount=1;
        // Growth is only meaningful when there is a previous size to compare
        // against. On the very first tick the file has not changed; it has simply
        // been seen for the first time.
        sample.hasGrown=previousEntry!=previousSizes.end()
            && sample.currentSizeBytes>sample.previousSizeBytes;
        sample.isAboveWarningThreshold=sample.currentSizeBytes>=serviceConfiguration.warningThresholdBytes;
        sample.isAboveCriticalThreshold=sample.currentSizeBytes>=serviceConfiguration.criticalThresholdBytes;
        // The previous size is written back immediately, so a database that vanishes
        // between ticks simply stops appearing rather than being reported as having
        // shrunk to nothing.
        previousSizes[currentPath]=sample.currentSizeBytes;
        summary.measuredDatabaseCount++;
        if (sample.isAboveWarningThreshold) {
            summary.warningCount++;
        }
        if (sample.isAboveCriticalThreshold) {
            summary.criticalCount++;
        }
        if (sample.hasGrown) {
            summary.grownCount++;
        }
        // A line is built only when there is something to say. That is what keeps
        // the idle cost at one comparison per database rather than a string
        // allocation.
        if (sample.isAboveCriticalThreshold) {
            summary.messagesToLog.push_back(
                string(SERVICE_NAME) + ": " + currentPath + " is "
                + HumanReadableFormatting::formatBytes(sample.currentSizeBytes)
                + ", which is above the urgent threshold of "
                + HumanReadableFormatting::formatBytes(serviceConfiguration.criticalThresholdBytes)
                + ". Run: agent-db-doctor diagnose --database \"" + currentPath + "\""
            );
        } else if (sample.isAboveWarningThreshold) {
            summary.messagesToLog.push_back(
                string(SERVICE_NAME) + ": " + currentPath + " is "
                + HumanReadableFormatting::formatBytes(sample.currentSizeBytes)
                + ", which is above the warning threshold of "
                + HumanReadableFormatting::formatBytes(serviceConfiguration.warningThresholdBytes)
                + ". Run: agent-db-doctor diagnose --database \"" + currentPath + "\""
            );
        } else if (sample.hasGrown && sample.previousSizeBytes>0) {
            summary.messagesToLog.push_back(
                string(SERVICE_NAME) + ": " + currentPath + " grew from "
                + HumanReadableFormatting::formatBytes(sample.previousSizeBytes) + " to "
                + HumanReadableFormatting::formatBytes(sample.currentSizeBytes) + " in the last hour."
            );
        }
        summary.samples.push_back(std::move(sample));
    }
    const long long endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    summary.totalMetadataCallCount=static_cast<int>(PlatformMetadata::metadataCallCount()-callsBeforePass);
    summary.durationNanoseconds=endNanoseconds-startNanoseconds;
    // The schema fingerprints are refreshed once an hour, after the measured part of
    // the tick, so the per database cost the contract describes stays exactly one
    // metadata call. Fingerprinting means opening each database read only for the
    // length of one small query, which is reported in the documentation rather than
    // hidden, and it is what lets a later diagnose say that an agent changed its
    // layout.
    if (endNanoseconds-lastFingerprintRefreshNanoseconds>=WATCHED_PATHS_REFRESH_NANOSECONDS) {
        const long long currentEpochSeconds=static_cast<long long>(std::time(nullptr));
        for (const WatchdogSample& sample : summary.samples) {
            // The size is already known from the tick, so the history costs one
            // append per changed database per hour and nothing on an idle machine.
            HistoryStore::recordSize(sample.databaseFilePath, sample.currentSizeBytes, currentEpochSeconds);
        }
        for (const string& currentPath : watchedPaths) {
            try {
                Database watchedDatabase(currentPath, Database::OPEN_READ_ONLY);
                watchedDatabase.setBusyTimeoutMilliseconds(500);
                const string currentFingerprint=SchemaFingerprint::fingerprintOf(watchedDatabase);
                SchemaFingerprint::storeFingerprint(currentPath, currentFingerprint);
            } catch (const DatabaseOpenException&) {
                // A database that cannot be read gets no fingerprint; the diagnose
                // command reports the same problem with better words.
            }
        }
        lastFingerprintRefreshNanoseconds=endNanoseconds;
    }
    return summary;
}
void ServiceManager::runWatchdogLoop() {
    // The log is opened once and held open for the life of the process, which is
    // what lets each line cost a single write rather than an open, a write, and a
    // close.
    Logger logger(serviceConfiguration.logFilePath);
    logger.info(string(SERVICE_NAME) + " started, watching every tick.");
    while (!stopRequested) {
        const WatchdogRunSummary summary=runOneWatchdogPass();
        // The lines are written after the pass rather than during it, so the pass
        // itself stays free of file writes and the number of system calls per tick
        // stays equal to the number of databases.
        for (const string& lineToLog : summary.messagesToLog) {
            logger.warn(lineToLog);
        }
        // The loop sleeps in short slices so that a stop request is noticed quickly.
        // The total slept is the configured interval, so the frequency is exactly
        // what the user asked for. The arithmetic is 64 bit and the interval is
        // bounded by the configuration validation, so a very large interval can no
        // longer overflow into a negative slice count and turn the loop into a spin.
        const long long totalSlices=
            static_cast<long long>(serviceConfiguration.checkIntervalSeconds)
            * (1000 / SLEEP_SLICE_MILLISECONDS);
        for (long long sliceIndex=0; sliceIndex<totalSlices && !stopRequested; ++sliceIndex) {
            PlatformMetadata::sleepMilliseconds(SLEEP_SLICE_MILLISECONDS);
        }
    }
    logger.info(string(SERVICE_NAME) + " stopped.");
}
void ServiceManager::install() {
    string validationExplanation;
    if (!serviceConfiguration.isValid(validationExplanation)) {
        throw ConfigurationException(validationExplanation);
    }
    // The configuration is written first, so that a service which starts
    // immediately has something to read.
    ConfigurationFile::write(serviceConfiguration, serviceConfiguration.configurationFilePath);
    // The platform definition is written second, because it refers to the
    // configuration path above.
    writePlatformServiceDefinition();
    startPlatformService();
}
void ServiceManager::uninstall() {
    stopPlatformService();
    removePlatformServiceDefinition();
    // The configuration file is deliberately left alone. A person uninstalling a
    // tool because they are tidying up did not ask to have their thresholds erased,
    // and reinstalling should find their settings still there.
}
void ServiceManager::start() {
    startPlatformService();
}
void ServiceManager::stop() {
    stopPlatformService();
}
ServiceStatusSnapshot ServiceManager::readStatus() {
    ServiceStatusSnapshot status=readPlatformStatus();
    status.installationDescription=serviceConfiguration.describeLocations();
    string stateText;
    if (!status.isInstalled) {
        stateText="Not installed. Nothing is being watched.";
    } else if (status.isRunning) {
        stateText=string("Running. Watching ") + to_string(resolveWatchedDatabasePaths().size())
            + (resolveWatchedDatabasePaths().size()==1 ? " database" : " databases")
            + " every " + to_string(serviceConfiguration.checkIntervalSeconds) + " seconds.";
    } else {
        stateText="Installed but not running.";
    }
    status.stateDescription=stateText;
    // The most recent log line is read so that a user checking the service does not
    // have to go and find the log file themselves. The file is small, because the
    // service writes at most one line an hour.
    const FileMetadata logStatus=PlatformMetadata::readFileMetadata(serviceConfiguration.logFilePath);
    if (logStatus.exists && logStatus.logicalSizeBytes>0) {
        std::ifstream logFile(serviceConfiguration.logFilePath);
        if (logFile.is_open()) {
            string line;
            string lastLine;
            while (getline(logFile, line)) {
                if (!line.empty()) {
                    lastLine=line;
                }
            }
            status.mostRecentLogLine=lastLine;
        }
    }
    return status;
}
}
