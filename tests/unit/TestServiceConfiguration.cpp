// The unit tests for the service configuration file.
//
// The configuration holds the thresholds a user chooses, so the tests are about a
// value surviving a round trip through the disk, and about a malformed file being
// refused with a line number rather than with a shrug.
#include <string>
// Provides `ofstream`, used to write a configuration file for the tests to read.
#include <fstream>
// Provides `remove`, which clears the file a test wrote.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the classes under test.
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
#include "AgentDbDoctor/Presentation/ProgressBar.hpp"
// Provides the exception type the configuration reader raises.
#include "AgentDbDoctor/Common/Exceptions.hpp"
// Provides the synthetic database builder, for the scratch folder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Service;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("a configuration round trips through a file", "[configuration]") {
    const string configurationPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestConfiguration")
        + ".json";
    std::filesystem::remove(configurationPath);
    ServiceConfiguration configuration;
    configuration.configurationFilePath=configurationPath;
    configuration.logFilePath="/tmp/agent-db-doctor.log";
    configuration.warningThresholdBytes=12345678901LL;
    configuration.criticalThresholdBytes=98765432109LL;
    configuration.checkIntervalSeconds=1800;
    configuration.allowAutomaticPruning=true;
    configuration.watchedDatabasePaths.push_back("/home/ann/.local/share/opencode/opencode.db");
    ConfigurationFile::write(configuration, configurationPath);
    const optional<ServiceConfiguration> readBack=ConfigurationFile::read(configurationPath);
    // Every field must survive the round trip, or a user who set a threshold would
    // find it silently reverted.
    REQUIRE(readBack.has_value());
    REQUIRE(readBack->warningThresholdBytes==12345678901LL);
    REQUIRE(readBack->criticalThresholdBytes==98765432109LL);
    REQUIRE(readBack->checkIntervalSeconds==1800);
    REQUIRE(readBack->allowAutomaticPruning);
    REQUIRE(readBack->watchedDatabasePaths.size()==1);
    REQUIRE(readBack->watchedDatabasePaths.front()
        =="/home/ann/.local/share/opencode/opencode.db");
    std::filesystem::remove(configurationPath);
}
TEST_CASE("a missing configuration file is not a failure", "[configuration]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestConfigurationMissing")
        + ".json";
    std::filesystem::remove(missingPath);
    // A first run has no file, and that is normal rather than a mistake.
    REQUIRE_FALSE(ConfigurationFile::read(missingPath).has_value());
}
TEST_CASE("a malformed configuration file names the line", "[configuration]") {
    const string configurationPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestConfigurationBad")
        + ".json";
    {
        std::ofstream brokenFile(configurationPath);
        // The colon is missing after the member name, which is the mistake a person
        // is most likely to make by hand.
        brokenFile << "{\n  \"checkIntervalSeconds\" 3600\n}\n";
    }
    // The error must name the line, because being told the file is invalid without a
    // line is exactly the unhelpful message this tool is meant to replace.
    REQUIRE_THROWS_AS(ConfigurationFile::read(configurationPath), ConfigurationException);
    try {
        ConfigurationFile::read(configurationPath);
        FAIL("the file should have been refused");
    } catch (const ConfigurationException& parseFailure) {
        REQUIRE(parseFailure.message().find("line")!=string::npos);
    }
    std::filesystem::remove(configurationPath);
}
TEST_CASE("a configuration that is not an object is refused", "[configuration]") {
    const string configurationPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestConfigurationNotObject")
        + ".json";
    {
        std::ofstream brokenFile(configurationPath);
        brokenFile << "[1, 2, 3]";
    }
    REQUIRE_THROWS_AS(ConfigurationFile::read(configurationPath), ConfigurationException);
    std::filesystem::remove(configurationPath);
}
TEST_CASE("a configuration with only some fields keeps the rest of the defaults", "[configuration]") {
    const string configurationPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestConfigurationPartial")
        + ".json";
    {
        std::ofstream partialFile(configurationPath);
        partialFile << "{ \"checkIntervalSeconds\": 600 }";
    }
    const optional<ServiceConfiguration> readBack=ConfigurationFile::read(configurationPath);
    // A user who wants to change one number should not have to write out the rest.
    REQUIRE(readBack.has_value());
    REQUIRE(readBack->checkIntervalSeconds==600);
    REQUIRE(readBack->warningThresholdBytes>0);
    std::filesystem::remove(configurationPath);
}
TEST_CASE("a path with a backslash survives the round trip", "[configuration]") {
    // A Windows path contains backslashes, which are the one escape a hand written
    // configuration file has to get right, so it is tested directly.
    const string configurationPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestConfigurationWindows")
        + ".json";
    std::filesystem::remove(configurationPath);
    ServiceConfiguration configuration;
    configuration.configurationFilePath=configurationPath;
    configuration.watchedDatabasePaths.push_back("C:\\Users\\ann\\AppData\\Local\\opencode\\opencode.db");
    ConfigurationFile::write(configuration, configurationPath);
    const optional<ServiceConfiguration> readBack=ConfigurationFile::read(configurationPath);
    REQUIRE(readBack.has_value());
    REQUIRE(readBack->watchedDatabasePaths.size()==1);
    REQUIRE(readBack->watchedDatabasePaths.front()
        =="C:\\Users\\ann\\AppData\\Local\\opencode\\opencode.db");
    std::filesystem::remove(configurationPath);
}
TEST_CASE("an inconsistent configuration is refused with a reason", "[configuration]") {
    ServiceConfiguration configuration;
    // A critical threshold below the warning one would mean the warning never fires.
    configuration.warningThresholdBytes=1000;
    configuration.criticalThresholdBytes=500;
    string explanation;
    REQUIRE_FALSE(configuration.isValid(explanation));
    REQUIRE(explanation.find("critical")!=string::npos);
    // An interval of one second would spin the watchdog rather than watch anything.
    configuration.criticalThresholdBytes=2000;
    configuration.checkIntervalSeconds=1;
    REQUIRE_FALSE(configuration.isValid(explanation));
    REQUIRE(explanation.find("ten seconds")!=string::npos);
    // A correct configuration passes and leaves no complaint.
    configuration.checkIntervalSeconds=3600;
    REQUIRE(configuration.isValid(explanation));
    REQUIRE(configuration.describeLocations().find("Configuration:")!=string::npos);
}
TEST_CASE("the default configuration watches everything", "[configuration]") {
    const ServiceConfiguration configuration=ServiceConfiguration::createDefault();
    // An empty watched list means every supported agent, which is the right default
    // because the whole point of the tool is to find problems nobody was looking for.
    REQUIRE(configuration.watchedDatabasePaths.empty());
    REQUIRE_FALSE(configuration.configurationFilePath.empty());
    REQUIRE_FALSE(configuration.logFilePath.empty());
    // Automatic pruning is off, because a tool that changes a database while nobody
    // is watching is a tool nobody trusts.
    REQUIRE_FALSE(configuration.allowAutomaticPruning);
}
