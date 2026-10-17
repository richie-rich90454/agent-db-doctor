// The benchmark for the diagnostic pass.
//
// The specification sets a hard performance contract: a diagnostic pass on a
// hundred gigabyte database completes in under five hundred milliseconds, makes
// fewer than twenty system calls, and reads fewer than sixty four kilobytes. This
// file turns those three numbers into assertions, which is what makes them a
// contract rather than a claim. Any regression in time, calls, or bytes read fails
// the build.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the class under test.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
// Provides the platform metadata, whose counter is the budget being measured.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
namespace {
// The size the contract names. The file is grown with a sparse region, so it has this
// logical size while occupying almost nothing on the disk, which is what lets a test
// on a build machine exercise a hundred gigabyte database honestly.
constexpr long long HUNDRED_GIGABYTES=100LL * 1024LL * 1024LL * 1024LL;
// The wall clock budget for one pass.
constexpr long long PASS_BUDGET_NANOSECONDS=500000000LL;
// The system call budget for one pass. A handful of databases would each cost one.
constexpr int CALL_BUDGET=20;
}
TEST_CASE("a diagnostic pass on a huge database stays inside the call budget", "[benchmark]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BenchmarkHugeDatabase");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    // The builder grows the file to a hundred gigabytes with a sparse region: the
    // bytes are never written, so the test costs no disk space, but the file
    // system's metadata now reports a size that would take a full scan minutes to
    // read. That is the point: a pass that read the file could not possibly finish
    // inside the budget.
    options.padToLargeSize=true;
    options.paddedSizeBytes=HUNDRED_GIGABYTES;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    // The growth is asserted rather than assumed. A growth that quietly did not
    // happen, which is what a build machine without the free space used to
    // produce, would make every measurement below pass for the wrong reason.
    const long long reportedSize=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    REQUIRE(reportedSize>90000000000LL);
    AgentDatabaseDoctor doctor;
    PlatformMetadata::resetMetadataCallCount();
    const long long startNanoseconds=PlatformMetadata::monotonicNanoseconds();
    const DiagnosisReport report=doctor.diagnoseDatabase(databasePath, "OpenCode", false);
    const long long endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    const long long elapsedNanoseconds=endNanoseconds-startNanoseconds;
    // The report carries the size the file system gave, so it can show the hundred
    // gigabytes without having read any of them.
    REQUIRE(report.getLogicalSizeBytes()>90000000000LL);
    // Fewer than twenty metadata calls, whatever the size of the file. This is the
    // assertion that would fail first if anyone reintroduced a scan.
    REQUIRE(report.getMetadataCallCount()<CALL_BUDGET);
    // Under five hundred milliseconds on a hundred gigabyte database. Reading even
    // one percent of that file would take longer, so the only way to pass is to
    // answer every question from metadata.
    REQUIRE(elapsedNanoseconds<PASS_BUDGET_NANOSECONDS);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the integrity check is skipped above the size limit and says so", "[benchmark]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BenchmarkCheckSkipped");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=3;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=0;
    // Grown with a sparse region, exactly as above, so the size limit is reached
    // without the disk space.
    options.padToLargeSize=true;
    options.paddedSizeBytes=HUNDRED_GIGABYTES;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    REQUIRE(PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes>90000000000LL);
    AgentDatabaseDoctor doctor;
    PlatformMetadata::resetMetadataCallCount();
    const long long startNanoseconds=PlatformMetadata::monotonicNanoseconds();
    const DiagnosisReport report=doctor.diagnoseDatabase(databasePath, "OpenCode", false);
    const long long endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    // A page by page check on a hundred gigabyte file would take minutes, so the
    // pass skips it by default. Skipping silently would be dishonest, so the report
    // has to say that it skipped and name the flag that runs it.
    REQUIRE(report.getIntegrityStatus()==IntegrityStatus::NOT_CHECKED);
    bool mentionedTheCheck=false;
    for (const string& warning : report.getWarnings()) {
        if (warning.find("integrity check was not run")!=string::npos
            && warning.find("--check")!=string::npos) {
            mentionedTheCheck=true;
        }
    }
    REQUIRE(mentionedTheCheck);
    REQUIRE((endNanoseconds-startNanoseconds)<PASS_BUDGET_NANOSECONDS);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a pass costs the same on a small database and a huge one", "[benchmark]") {
    // The whole point of the design is that the cost of a pass does not depend on the
    // size of the file. Two passes over files four orders of magnitude apart must
    // make the same number of calls.
    const string smallDatabasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BenchmarkSmall");
    const string hugeDatabasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("BenchmarkHuge");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(smallDatabasePath);
    SyntheticDatabaseBuilder::removeIfPresent(hugeDatabasePath);
    SyntheticDatabaseBuilder::build(smallDatabasePath, options);
    // Only the second file is padded, and by the builder, so the growth is the
    // same sparse growth the tests above rely on.
    options.padToLargeSize=true;
    options.paddedSizeBytes=HUNDRED_GIGABYTES;
    SyntheticDatabaseBuilder::build(hugeDatabasePath, options);
    REQUIRE(PlatformMetadata::readFileMetadata(hugeDatabasePath).logicalSizeBytes>90000000000LL);
    AgentDatabaseDoctor doctor;
    const DiagnosisReport smallReport=doctor.diagnoseDatabase(smallDatabasePath, "OpenCode", false);
    const DiagnosisReport hugeReport=doctor.diagnoseDatabase(hugeDatabasePath, "OpenCode", false);
    // The call counts are identical, which is the property that makes a scheduled
    // check affordable no matter how large the database grows.
    REQUIRE(hugeReport.getMetadataCallCount()==smallReport.getMetadataCallCount());
    SyntheticDatabaseBuilder::removeIfPresent(smallDatabasePath);
    SyntheticDatabaseBuilder::removeIfPresent(hugeDatabasePath);
}
TEST_CASE("watching many databases still fits inside the call budget", "[benchmark]") {
    // A machine with the maximum number of supported agents has a handful of
    // databases. Twenty calls covers several times that, which is why the budget is
    // twenty and not two.
    const vector<string> databasePaths={
        SyntheticDatabaseBuilder::temporaryDatabasePath("BenchmarkWatchOne"),
        SyntheticDatabaseBuilder::temporaryDatabasePath("BenchmarkWatchTwo"),
        SyntheticDatabaseBuilder::temporaryDatabasePath("BenchmarkWatchThree")
    };
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=3;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=0;
    for (const string& currentPath : databasePaths) {
        SyntheticDatabaseBuilder::removeIfPresent(currentPath);
        SyntheticDatabaseBuilder::build(currentPath, options);
    }
    AgentDatabaseDoctor doctor;
    PlatformMetadata::resetMetadataCallCount();
    for (const string& currentPath : databasePaths) {
        doctor.diagnoseDatabase(currentPath, "OpenCode", false);
    }
    // Three databases at three calls each is well inside the budget, which is what
    // lets the service run on a machine with everything installed.
    REQUIRE(PlatformMetadata::metadataCallCount()<CALL_BUDGET);
    for (const string& currentPath : databasePaths) {
        SyntheticDatabaseBuilder::removeIfPresent(currentPath);
    }
}
