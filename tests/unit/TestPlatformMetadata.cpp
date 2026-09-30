// The unit tests for the PlatformMetadata class.
//
// This class is the tool's only window onto the file system, and the performance
// contract is written in terms of how many times it is allowed to ask. The tests
// check both the answers and the call count, because the count is the part that a
// regression would hide.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the class under test.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder, for a real file to measure.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("a file that is not there is reported as not there", "[platform]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestPlatformMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    const FileMetadata metadata=PlatformMetadata::readFileMetadata(missingPath);
    REQUIRE_FALSE(metadata.exists);
    REQUIRE(metadata.logicalSizeBytes==0);
    // The error code is kept so the report can tell "not there" from "there but
    // unreadable", which are very different problems.
    REQUIRE(metadata.errorCode!=0);
}
TEST_CASE("a real file reports its size and its type", "[platform]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestPlatformFile");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(
        databasePath,
        SyntheticDatabaseBuilder::Options()
    );
    const FileMetadata metadata=PlatformMetadata::readFileMetadata(databasePath);
    REQUIRE(metadata.exists);
    REQUIRE_FALSE(metadata.isDirectory);
    // A real SQLite database is never empty, so a non zero size is the check that
    // the answer is real rather than a default.
    REQUIRE(metadata.logicalSizeBytes>0);
    REQUIRE(metadata.allocatedSizeBytes>0);
    REQUIRE(metadata.modificationEpochMilliseconds>0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a folder is told apart from a file", "[platform]") {
    const string folderPath=SyntheticDatabaseBuilder::temporaryFolderPath();
    const FileMetadata metadata=PlatformMetadata::readFileMetadata(folderPath);
    REQUIRE(metadata.exists);
    // The distinction matters because several supported agents keep their history
    // in a folder rather than in a database.
    REQUIRE(metadata.isDirectory);
}
TEST_CASE("the call counter counts exactly one call per query", "[platform]") {
    PlatformMetadata::resetMetadataCallCount();
    REQUIRE(PlatformMetadata::metadataCallCount()==0);
    // One query is one call, which is the whole of the watchdog's budget per tick.
    PlatformMetadata::readFileMetadata("a path that does not matter");
    REQUIRE(PlatformMetadata::metadataCallCount()==1);
    PlatformMetadata::readFileMetadata("a path that does not matter either");
    REQUIRE(PlatformMetadata::metadataCallCount()==2);
    // The counter is reset rather than subtracted, so a test always starts from a
    // known value.
    PlatformMetadata::resetMetadataCallCount();
    REQUIRE(PlatformMetadata::metadataCallCount()==0);
}
TEST_CASE("the free space of the volume is readable", "[platform]") {
    const string folderPath=SyntheticDatabaseBuilder::temporaryFolderPath();
    // The free space call needs a path that exists, so the scratch folder is used
    // rather than a database file that no test has created yet.
    const DiskSpace space=PlatformMetadata::readDiskSpace(folderPath);
    // The answer depends on the machine, so the test checks the shape rather than
    // the numbers: a valid answer with a total larger than what is free.
    REQUIRE(space.isValid);
    REQUIRE(space.totalBytes>0);
    REQUIRE(space.availableBytes<=space.totalBytes);
    REQUIRE(space.errorCode==0);
}
TEST_CASE("the monotonic clock never goes backwards", "[platform]") {
    const long long firstReading=PlatformMetadata::monotonicNanoseconds();
    const long long secondReading=PlatformMetadata::monotonicNanoseconds();
    // A wall clock that jumped backwards would produce a negative measurement, which
    // is why the tool uses this clock rather than the time of day.
    REQUIRE(secondReading>=firstReading);
    REQUIRE(firstReading>0);
}
TEST_CASE("the process identifier and the host name are available", "[platform]") {
    REQUIRE(PlatformMetadata::currentProcessIdentifier()>0);
    // A process always exists while it is running, so asking about itself must
    // succeed. That is the check the tool uses to tell whether an agent is open.
    REQUIRE(PlatformMetadata::isProcessRunning(PlatformMetadata::currentProcessIdentifier()));
    // An identifier that could never exist is reported as not running, so a caller
    // never waits on a process that is not there.
    REQUIRE_FALSE(PlatformMetadata::isProcessRunning(-1));
    REQUIRE_FALSE(PlatformMetadata::hostName().empty());
}
TEST_CASE("an environment variable is read when it is set", "[platform]") {
    string readValue;
    // A variable that certainly exists on every platform, used to check the happy
    // path without depending on the machine's configuration.
    REQUIRE(PlatformMetadata::readEnvironmentVariable("PATH", readValue));
    REQUIRE_FALSE(readValue.empty());
    // A variable that certainly does not exist is reported as absent rather than
    // returning an empty string, so the two cases can be told apart.
    string missingValue="left alone";
    REQUIRE_FALSE(PlatformMetadata::readEnvironmentVariable(
        "AGENT_DB_DOCTOR_A_VARIABLE_THAT_IS_NOT_SET",
        missingValue
    ));
    REQUIRE(missingValue=="left alone");
}
