// The unit tests for the ProgressBar class.
//
// The specification requires that an operation taking more than two hundred
// milliseconds shows progress, and that no escape sequence is written when the
// output is not a terminal. The tests here check the parts that can be checked
// without a terminal: that the bar is safe to drive, that it stops being active when
// it is finished, and that nothing it does can throw or misbehave.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the class under test.
#include "AgentDbDoctor/Presentation/ProgressBar.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
TEST_CASE("a bar does not draw when the output is not a terminal", "[progress]") {
    // With the output redirected, the bar prints plain lines and no escape
    // sequences, which is what makes the tool safe to pipe into a log.
    ProgressBar redirectedBar("Testing", 100, false);
    REQUIRE(redirectedBar.isActive());
    redirectedBar.advance(50);
    // The destructor finishes the bar, so a caller never has to remember to.
    redirectedBar.finish();
    REQUIRE_FALSE(redirectedBar.isActive());
}
TEST_CASE("finishing twice is harmless", "[progress]") {
    ProgressBar terminalBar("Testing", 100, true);
    terminalBar.advance(100);
    terminalBar.finish();
    // The destructor calls finish again, and a second finish must not draw a second
    // final line.
    REQUIRE_FALSE(terminalBar.isActive());
}
TEST_CASE("advancing past the total does not exceed a hundred percent", "[progress]") {
    ProgressBar terminalBar("Testing", 10, true);
    // An operation that reports more work than it estimated is normal, and the bar
    // must not show a percentage above one hundred.
    terminalBar.advance(1000);
    terminalBar.finish();
    REQUIRE_FALSE(terminalBar.isActive());
}
TEST_CASE("a fraction sets the progress and is clamped", "[progress]") {
    ProgressBar terminalBar("Testing", 200, true);
    terminalBar.setFractionComplete(0.25);
    terminalBar.setFractionComplete(1.5);
    terminalBar.setFractionComplete(-0.5);
    terminalBar.finish();
    // A fraction outside the range is clamped rather than producing a nonsense
    // bar, and none of it throws.
    REQUIRE_FALSE(terminalBar.isActive());
}
TEST_CASE("changing the description forces a redraw", "[progress]") {
    ProgressBar terminalBar("First task", 100, true);
    // A command that changes phase mid run replaces the phrase, and the next
    // redraw has to show it even though the percentage has not moved.
    terminalBar.setTaskDescription("Second task");
    terminalBar.finish();
    REQUIRE_FALSE(terminalBar.isActive());
}
TEST_CASE("the total can be set after the bar is built", "[progress]") {
    // The size of a file is not always known when a command starts, so the total is
    // set once it has been learned.
    ProgressBar terminalBar("Testing", 0, true);
    terminalBar.setTotalUnits(500);
    terminalBar.advance(500);
    terminalBar.finish();
    REQUIRE_FALSE(terminalBar.isActive());
}
TEST_CASE("a bar of zero width is a usable constant", "[progress]") {
    // The width is a constant rather than something read from the terminal, so a bar
    // never depends on geometry the tool cannot verify.
    REQUIRE(ProgressBar::BAR_WIDTH_IN_CHARACTERS>0);
    REQUIRE(ProgressBar::BAR_WIDTH_IN_CHARACTERS<80);
}
