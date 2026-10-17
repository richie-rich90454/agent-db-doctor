// The unit tests for the graphical action worker.
//
// The worker runs commands on its own thread and publishes through the
// progress sink. These tests drive it headlessly against a synthetic database:
// a preview must finish with numbers and change nothing, and a finished run
// must hand its result over exactly once.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the action runner under test.
#include "ActionState.hpp"
// Provides the database wrapper, so the tests can prove a preview changed nothing.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the platform metadata, for the file sizes.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder, so the tests work on real files.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor::Gui;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Counts the rows of the event log, which is what a compaction would change.
long long countEventRows(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    return database.querySingleInteger("SELECT COUNT(*) FROM event").value_or(-1);
}
}
TEST_CASE("a compaction preview finishes with numbers and changes nothing", "[actions]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ActionPreview");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=4;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::build(databasePath, options);
    // The preview's claim in its own name is that it changes nothing, so the row
    // count is captured before and compared after. Without this comparison the
    // test would pass even if the preview deleted rows.
    const long long eventRowsBefore=countEventRows(databasePath);
    ActionRunner runner;
    ActionRequest request;
    request.kind=ActionKind::CompactionPreview;
    request.databasePath=databasePath;
    runner.start(request);
    runner.join();
    REQUIRE_FALSE(runner.isBusy());
    ActionResult result;
    REQUIRE(runner.takeResult(result));
    REQUIRE(result.kind==ActionKind::CompactionPreview);
    REQUIRE(result.exitCode==0);
    REQUIRE_FALSE(result.summary.empty());
    REQUIRE(result.summary.find("Nothing was changed.")!=string::npos);
    REQUIRE(countEventRows(databasePath)==eventRowsBefore);
    // A second take finds nothing: each result is seen exactly once.
    ActionResult noResult;
    REQUIRE_FALSE(runner.takeResult(noResult));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a refused action still carries its explanation", "[actions]") {
    // The result dialogue shows `summary` as its body, so a command that refuses
    // to run must fill it, or the user sees an empty window with no reason.
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ActionRefusal");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    ActionRunner runner;
    ActionRequest request;
    request.kind=ActionKind::CompactionPreview;
    request.databasePath=databasePath;
    runner.start(request);
    runner.join();
    ActionResult result;
    REQUIRE(runner.takeResult(result));
    REQUIRE(result.exitCode!=0);
    REQUIRE_FALSE(result.summary.empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a vacuum preview reports its numbers and changes nothing", "[actions]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ActionProgress");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=4;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    ActionRunner runner;
    ActionRequest request;
    request.kind=ActionKind::VacuumPreview;
    request.databasePath=databasePath;
    runner.start(request);
    runner.join();
    ActionResult result;
    REQUIRE(runner.takeResult(result));
    REQUIRE(result.kind==ActionKind::VacuumPreview);
    REQUIRE(result.exitCode==0);
    REQUIRE_FALSE(result.summary.empty());
    REQUIRE(result.summary.find("Nothing was changed.")!=string::npos);
    // A preview promises the file is untouched, so its size is unchanged too.
    REQUIRE(PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes==sizeBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
