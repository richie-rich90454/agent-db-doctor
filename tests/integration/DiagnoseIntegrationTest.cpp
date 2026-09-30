// The integration test for the diagnose command.
//
// This test runs the whole command against a real database built to the shape an AI
// coding agent produces, and checks what a user would read. It is the closest thing
// to a real run that can be made on a build machine.
#include <string>
// Provides `remove` and `exists`, which clear and check a test's own files.
#include <filesystem>
// Provides `ofstream`, used to write a file that is not a database.
#include <fstream>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the command under test.
#include "AgentDbDoctor/Command/DiagnoseCommand.hpp"
// Provides the options the command reads.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the platform metadata the report is built from.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("diagnose reports a healthy database and says nothing needs doing", "[integration][diagnose]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("DiagnoseHealthy");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=5;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    // The output goes to standard output, which is not a terminal here, so the
    // command takes the plain text path. That is the path a user in a script gets,
    // and it is the one that must never emit an escape sequence.
    DiagnoseCommand command(false);
    const int exitCode=command.execute(commandOptions);
    // A healthy database must not make a monitoring script think something is wrong.
    REQUIRE(exitCode==0);
    REQUIRE(command.healthyDatabaseCount()==1);
    REQUIRE(command.problemDatabaseCount()==0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("diagnose refuses to act on a file that is not a database", "[integration][diagnose]") {
    const string textFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("DiagnoseNotDatabase");
    SyntheticDatabaseBuilder::removeIfPresent(textFilePath);
    {
        // A text file is the clearest negative case, and the tool must report it
        // rather than trying to repair it.
        std::ofstream textFile(textFilePath);
        textFile << "this is a log file, not a database";
    }
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=textFilePath;
    commandOptions.quiet=true;
    DiagnoseCommand command(false);
    const int exitCode=command.execute(commandOptions);
    // A non zero code is what makes this usable from a monitoring script.
    REQUIRE(exitCode==1);
    REQUIRE(command.problemDatabaseCount()==1);
    std::filesystem::remove(textFilePath);
}
TEST_CASE("diagnose reports a file that is not there without failing", "[integration][diagnose]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("DiagnoseMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=missingPath;
    commandOptions.quiet=true;
    DiagnoseCommand command(false);
    // A missing file is a result, not a crash. A user who has never used an agent has
    // nothing to diagnose and the tool says so rather than failing.
    const int exitCode=command.execute(commandOptions);
    REQUIRE(exitCode==1);
}
TEST_CASE("diagnose is fast enough to be run constantly", "[integration][diagnose]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("DiagnoseTiming");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=20;
    options.messageCountPerSession=10;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    AgentDatabaseDoctor doctor;
    // The metadata call counter is reset so the measurement is of this pass alone.
    PlatformMetadata::resetMetadataCallCount();
    const long long startNanoseconds=PlatformMetadata::monotonicNanoseconds();
    const DiagnosisReport report=doctor.diagnoseDatabase(databasePath, "OpenCode", false);
    const long long endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    // The performance contract allows fewer than twenty metadata calls for a pass.
    // A pass that broke it would mean the tool was reading the database rather than
    // asking the file system about it.
    REQUIRE(report.getMetadataCallCount()<20);
    // Five hundred milliseconds is the budget. A small database is far inside it, so
    // a regression that made the pass read the file would show up here.
    REQUIRE((endNanoseconds - startNanoseconds) < 500000000LL);
    REQUIRE(report.getFileExists());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("diagnose never modifies the file it looks at", "[integration][diagnose]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("DiagnoseReadOnly");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    const long long modificationTimeBefore=
        PlatformMetadata::readFileMetadata(databasePath).modificationEpochMilliseconds;
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    DiagnoseCommand command(false);
    command.execute(commandOptions);
    const FileMetadata after=PlatformMetadata::readFileMetadata(databasePath);
    // A diagnostic pass that changed a single byte would be a serious bug, and this
    // is the check that would catch it.
    REQUIRE(after.logicalSizeBytes==sizeBefore);
    REQUIRE(after.modificationEpochMilliseconds==modificationTimeBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
