// The integration test for the service command and the watchdog.
//
// The service is what turns this tool from something a person remembers to run
// into something that notices. The tests check the parts that matter and can be
// checked without installing anything: that a pass costs one metadata call per
// database, that growth is noticed, that a threshold crossing is reported, and that
// nothing is written while a database stays below its threshold.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the service layer under test.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
// Provides the platform metadata, whose counter is the budget being checked.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Service;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
namespace {
// A configuration that watches one named file with a threshold low enough to be
// crossed, so the tests do not have to build a large database to see the behaviour.
ServiceConfiguration buildTestConfiguration(
    const string& watchedDatabasePath,
    long long warningThresholdBytes
) {
    ServiceConfiguration configuration;
    configuration.watchedDatabasePaths.push_back(watchedDatabasePath);
    configuration.warningThresholdBytes=warningThresholdBytes;
    configuration.criticalThresholdBytes=warningThresholdBytes*2;
    configuration.checkIntervalSeconds=3600;
    configuration.configurationFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceConfiguration")
        + ".json";
    configuration.logFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceLog");
    return configuration;
}
}
TEST_CASE("a watchdog pass costs one metadata call per database", "[integration][service]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceTickCost");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    ServiceManager serviceManager(buildTestConfiguration(databasePath, 100000000000LL));
    // The counter is reset so the measurement is of this pass alone.
    PlatformMetadata::resetMetadataCallCount();
    const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
    // One call for one database is the whole of the specification's budget. A pass
    // that read the file, or opened it, would cost more and would be caught here.
    REQUIRE(summary.totalMetadataCallCount==1);
    REQUIRE(summary.measuredDatabaseCount==1);
    REQUIRE(summary.durationNanoseconds>=0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a watchdog pass says nothing while a database stays small", "[integration][service]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceQuiet");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    ServiceManager serviceManager(buildTestConfiguration(databasePath, 100000000000LL));
    // The first pass records the size. The second sees no change, so there is nothing
    // worth saying and nothing is logged, which is what keeps the idle cost near
    // nothing.
    serviceManager.runOneWatchdogPass();
    const WatchdogRunSummary secondPass=serviceManager.runOneWatchdogPass();
    REQUIRE(secondPass.messagesToLog.empty());
    REQUIRE(secondPass.warningCount==0);
    REQUIRE(secondPass.grownCount==0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a watchdog pass notices a database that crossed the threshold", "[integration][service]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceThreshold");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // A threshold of one byte is crossed by any real database, which is how the test
    // checks the reporting without building a large file.
    ServiceManager serviceManager(buildTestConfiguration(databasePath, 1));
    const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
    REQUIRE(summary.warningCount==1);
    // The line has to name the file and the command, because a person reading the log
    // at a glance needs to know what to type.
    REQUIRE(summary.messagesToLog.size()==1);
    REQUIRE(summary.messagesToLog.front().find(databasePath)!=string::npos);
    REQUIRE(summary.messagesToLog.front().find("agent-db-doctor diagnose")!=string::npos);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a watchdog pass tells a warning from an emergency", "[integration][service]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceCritical");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // The critical threshold is below the database's size, so the pass must report it
    // as urgent rather than merely as worth watching.
    ServiceConfiguration configuration=buildTestConfiguration(databasePath, 1);
    configuration.criticalThresholdBytes=1;
    ServiceManager serviceManager(configuration);
    const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
    REQUIRE(summary.criticalCount==1);
    REQUIRE(summary.messagesToLog.front().find("urgent")!=string::npos);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a watchdog pass records the previous size so growth is visible", "[integration][service]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceGrowth");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    ServiceManager serviceManager(buildTestConfiguration(databasePath, 100000000000LL));
    // The first pass has nothing to compare against, so no growth is reported.
    const WatchdogRunSummary firstPass=serviceManager.runOneWatchdogPass();
    REQUIRE(firstPass.grownCount==0);
    REQUIRE(firstPass.samples.front().previousSizeBytes==0);
    // The second pass compares against the recorded size, which is how growth is
    // worked out without opening the database.
    const WatchdogRunSummary secondPass=serviceManager.runOneWatchdogPass();
    REQUIRE(secondPass.samples.front().previousSizeBytes>0);
    REQUIRE(secondPass.samples.front().currentSizeBytes
        ==secondPass.samples.front().previousSizeBytes);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a watchdog pass handles a database that is not there", "[integration][service]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    ServiceManager serviceManager(buildTestConfiguration(missingPath, 1));
    // A file that has gone is not an emergency. The pass measures it, finds nothing,
    // and says nothing, rather than reporting a size of zero as a fault.
    const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
    REQUIRE(summary.warningCount==0);
    REQUIRE(summary.criticalCount==0);
    REQUIRE(summary.messagesToLog.empty());
}
TEST_CASE("the service name is the same on every platform", "[integration][service]") {
    // A script written on one platform has to work on another, so the name is a
    // single constant rather than something each platform invents.
    REQUIRE(ServiceManager::serviceDisplayName()=="agent-db-doctor");
    REQUIRE_FALSE(ServiceManager::currentExecutablePath().empty());
}
TEST_CASE("the status is readable without installing anything", "[integration][service]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ServiceStatus");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=3;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    ServiceManager serviceManager(buildTestConfiguration(databasePath, 100000000000LL));
    const ServiceStatusSnapshot status=serviceManager.readStatus();
    // The status must always say something, whether or not the service is installed,
    // because it is the answer to a question a user just asked.
    REQUIRE_FALSE(status.stateDescription.empty());
    REQUIRE_FALSE(status.installationDescription.empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
