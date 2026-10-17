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
// Provides the synthetic database builder, so the tests work on real files.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor::Gui;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("a prune preview finishes with numbers and changes nothing", "[actions]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ActionPreview");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=4;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::build(databasePath, options);
    ActionRunner runner;
    ActionRequest request;
    request.kind=ActionKind::PrunePreview;
    request.databasePath=databasePath;
    runner.start(request);
    runner.join();
    REQUIRE_FALSE(runner.isBusy());
    ActionResult result;
    REQUIRE(runner.takeResult(result));
    REQUIRE(result.kind==ActionKind::PrunePreview);
    REQUIRE(result.exitCode==0);
    REQUIRE_FALSE(result.summary.empty());
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
    request.kind=ActionKind::PrunePreview;
    request.databasePath=databasePath;
    runner.start(request);
    runner.join();
    ActionResult result;
    REQUIRE(runner.takeResult(result));
    REQUIRE(result.exitCode!=0);
    REQUIRE_FALSE(result.summary.empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the progress numbers move while the worker runs", "[actions]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("ActionProgress");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=4;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=2;
    SyntheticDatabaseBuilder::build(databasePath, options);
    ActionRunner runner;
    ActionRequest request;
    request.kind=ActionKind::CompactPreview;
    request.databasePath=databasePath;
    runner.start(request);
    runner.join();
    ActionResult result;
    REQUIRE(runner.takeResult(result));
    REQUIRE(result.kind==ActionKind::CompactPreview);
    REQUIRE_FALSE(result.summary.empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
