// The integration test for the automation workstream: the automatic maintenance
// the watchdog runs on its own, the audit log it writes, and the configuration
// text that permits it.
//
// The tests drive a real ServiceManager over real SQLite files, because the
// behaviour that matters is the behaviour of the real commands: a database that
// is idle and above its urgent threshold must shrink, one that was written a
// moment ago must be left alone with the reason in the log, and every step must
// be findable afterwards in the audit log.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `remove`, which clears the store files the tests write.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the service layer under test.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
#include "AgentDbDoctor/Service/AutomaticMaintenance.hpp"
#include "AgentDbDoctor/Service/AuditLog.hpp"
#include "AgentDbDoctor/Service/HistoryStore.hpp"
// Provides the schema fingerprint store, which the same watchdog pass writes.
#include "AgentDbDoctor/Doctor/SchemaFingerprint.hpp"
// Provides the exception type the configuration reader raises.
#include "AgentDbDoctor/Common/Exceptions.hpp"
// Provides the database wrapper, used to add the free pages a vacuum undoes.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides PlatformMetadata, for the file sizes the tests compare.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Service;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Adds free pages to a database, which is the state a vacuum exists to undo.
// The pattern is the same one the insight command tests use: a table is filled,
// dropped, and the pages it held stay in the file as unused space.
void inflateWithFreePages(const string& databasePath, int freePageCount) {
    Database database(databasePath, Database::OPEN_READ_WRITE);
    database.execute("CREATE TABLE filler (id INTEGER PRIMARY KEY, body BLOB)");
    for (int pageIndex=0; pageIndex<freePageCount; ++pageIndex) {
        database.execute("INSERT INTO filler (body) VALUES (zeroblob(3000))");
    }
    database.execute("DROP TABLE filler");
}
// Builds the database every automation test works on: enough sessions and enough
// duplication that the compaction has something to remove, with free pages the
// vacuum can hand back.
string buildMaintainableDatabase(const string& testName) {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath(testName);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    // The session count is even so that the compaction's own limit, which keeps
    // the newest half, removes exactly half of a uniformly duplicated log rather
    // than refusing a change larger than its share.
    options.sessionCount=8;
    options.messageCountPerSession=4;
    options.redundantCopiesPerMessage=4;
    SyntheticDatabaseBuilder::build(databasePath, options);
    inflateWithFreePages(databasePath, 400);
    return databasePath;
}
// A configuration that watches one named file with thresholds low enough that any
// real database is above them, and with automatic changes permitted.
ServiceConfiguration buildMaintenanceConfiguration(const string& watchedDatabasePath) {
    ServiceConfiguration configuration;
    configuration.watchedDatabasePaths.push_back(watchedDatabasePath);
    configuration.warningThresholdBytes=1;
    configuration.criticalThresholdBytes=1;
    configuration.allowAutomaticCompaction=true;
    configuration.checkIntervalSeconds=3600;
    configuration.configurationFilePath=
        SyntheticDatabaseBuilder::temporaryDatabasePath("AutomationConfiguration") + ".json";
    configuration.logFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("AutomationLog");
    return configuration;
}
// Sends the stores every watchdog pass touches to the scratch folder, so a test
// never reads or writes the machine's real configuration directory.
void redirectStoresForTests(const string& testName) {
    HistoryStore::setStorePathForTests(
        SyntheticDatabaseBuilder::temporaryDatabasePath(testName + "History") + ".tsv"
    );
    SchemaFingerprint::setStorePathForTests(
        SyntheticDatabaseBuilder::temporaryDatabasePath(testName + "Fingerprint") + ".tsv"
    );
}
// True when one of the lines contains the fragment.
bool anyLineContains(const vector<string>& lines, const string& fragment) {
    for (const string& line : lines) {
        if (line.find(fragment)!=string::npos) {
            return true;
        }
    }
    return false;
}
}
TEST_CASE("the watchdog maintains an idle database above its critical threshold", "[integration][automation]") {
    const string databasePath=buildMaintainableDatabase("AutomationIdle");
    redirectStoresForTests("AutomationIdle");
    const string auditStorePath=SyntheticDatabaseBuilder::temporaryDatabasePath("AutomationAudit") + ".tsv";
    std::filesystem::remove(auditStorePath);
    AuditLog::setStorePathForTests(auditStorePath);
    const ServiceConfiguration configuration=buildMaintenanceConfiguration(databasePath);
    const long long sizeBeforeBytes=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    // The idle hook is what lets a test watch a real run. Zero minutes means the
    // database built a moment ago already counts as quiet; the default is fifteen.
    AutomaticMaintenance::setIdleMinutesForTests(0);
    ServiceManager serviceManager(configuration);
    const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
    AutomaticMaintenance::setIdleMinutesForTests(-1);
    // The pass must say what it did: a maintenance that ran without a log line
    // would be exactly the silent change the tool exists to avoid.
    REQUIRE(anyLineContains(summary.messagesToLog, "automatic maintenance"));
    // The file must actually be smaller afterwards: the compaction removes the
    // redundant rows and the vacuum hands the free pages back to the drive.
    const long long sizeAfterBytes=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    REQUIRE(sizeAfterBytes<sizeBeforeBytes);
    // Both steps must be in the audit log, newest last, with honest sizes.
    const vector<AuditEntry> entries=AuditLog::readAll();
    REQUIRE(entries.size()==2);
    REQUIRE(entries.front().action=="automatic-compaction");
    REQUIRE(entries.back().action=="automatic-vacuum");
    REQUIRE(entries.back().exitCode==0);
    REQUIRE(entries.back().sizeAfterBytes<entries.back().sizeBeforeBytes);
    std::filesystem::remove(auditStorePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the watchdog leaves a database written a moment ago alone", "[integration][automation]") {
    const string databasePath=buildMaintainableDatabase("AutomationInUse");
    redirectStoresForTests("AutomationInUse");
    // The default idle rule applies here, and the file was written by the line
    // above, so the maintenance must refuse and say so.
    AutomaticMaintenance::setIdleMinutesForTests(-1);
    const ServiceConfiguration configuration=buildMaintenanceConfiguration(databasePath);
    const long long sizeBeforeBytes=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    ServiceManager serviceManager(configuration);
    const WatchdogRunSummary summary=serviceManager.runOneWatchdogPass();
    REQUIRE(anyLineContains(summary.messagesToLog, "in use"));
    // The refusal must name the file, or a reader cannot tell which database was
    // skipped and why.
    REQUIRE(anyLineContains(summary.messagesToLog, databasePath));
    // Nothing may have changed: an agent that wrote a second ago is in use, and
    // the file must be exactly as it was.
    const long long sizeAfterBytes=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    REQUIRE(sizeAfterBytes==sizeBeforeBytes);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a per database policy survives the configuration text", "[integration][automation]") {
    const string configurationText=string("{\n")
        + "  \"watchedDatabases\": [\"C:\\\\data\\\\one.db\"],\n"
        + "  \"warningThresholdBytes\": 100,\n"
        + "  \"criticalThresholdBytes\": 1000,\n"
        + "  \"databasePolicies\": [{\"path\": \"C:\\\\data\\\\one.db\", "
          "\"criticalThresholdBytes\": 500, \"allowAutomaticCompaction\": true, "
          "\"notifyOnThreshold\": true}]\n"
        + "}";
    const optional<ServiceConfiguration> parsedConfiguration=
        ConfigurationFile::fromText(configurationText, "/tmp/config.json");
    REQUIRE(parsedConfiguration.has_value());
    REQUIRE(parsedConfiguration->databasePolicies.size()==1);
    const DatabasePolicy& policy=parsedConfiguration->databasePolicies.front();
    REQUIRE(policy.databaseFilePath=="C:\\data\\one.db");
    REQUIRE(policy.criticalThresholdBytes==500);
    REQUIRE(policy.allowAutomaticCompaction);
    REQUIRE(policy.notifyOnThreshold);
    // The lookup the service uses must find the policy by the exact path and must
    // not invent one for anything else.
    REQUIRE(parsedConfiguration->policyFor("C:\\data\\one.db")!=nullptr);
    REQUIRE(parsedConfiguration->policyFor("/data/other.db")==nullptr);
    // The rendered file must parse back to the same policy, or a user's settings
    // would be lost the next time the service rewrote the file.
    const optional<ServiceConfiguration> roundTrippedConfiguration=ConfigurationFile::fromText(
        ConfigurationFile::renderAsJson(parsedConfiguration.value()),
        "/tmp/config.json"
    );
    REQUIRE(roundTrippedConfiguration.has_value());
    REQUIRE(roundTrippedConfiguration->databasePolicies.size()==1);
    const DatabasePolicy& roundTrippedPolicy=roundTrippedConfiguration->databasePolicies.front();
    REQUIRE(roundTrippedPolicy.databaseFilePath=="C:\\data\\one.db");
    REQUIRE(roundTrippedPolicy.criticalThresholdBytes==500);
    REQUIRE(roundTrippedPolicy.allowAutomaticCompaction);
    REQUIRE(roundTrippedPolicy.notifyOnThreshold);
    // A policy that names no file would silently apply to nothing, so the text is
    // refused with the same exception a malformed file gets.
    REQUIRE_THROWS_AS(
        ConfigurationFile::fromText(
            "{\"databasePolicies\": [{\"criticalThresholdBytes\": 5}]}",
            "/tmp/config.json"
        ),
        ConfigurationException
    );
    // A malformed file is still refused through the same entry point.
    REQUIRE_THROWS_AS(
        ConfigurationFile::fromText("{\"checkIntervalSeconds\" 3600}", "/tmp/config.json"),
        ConfigurationException
    );
}
