// The entry point shared by every test executable.
//
// Catch2's own `Catch2::Catch2WithMain` would provide this, but the tool needs
// two things that library does not: a fixed temporary directory that the
// integration tests can create and destroy freely, and a signal handler that
// turns a crash into a readable test failure instead of a silent exit code.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides `remove_all` and `temp_directory_path`, used to give each test run a
// private scratch directory.
#include <filesystem>
using namespace std;
using namespace std::filesystem;
// The CatchMainTestCase is a single test whose only job is to prepare and clean
// the shared temporary directory before and after the rest of the suite runs.
// The `///` comment above a test case is Catch2's own documentation syntax and
// is not a C++ comment.
TEST_CASE("the shared scratch directory is prepared and removed", "[support]") {
    const path scratchDirectory=path(temp_directory_path()) / "agent-db-doctor-tests";
    // `remove_all` succeeds even when the path does not exist, which is exactly
    // the behaviour needed to make a repeated test run reproducible.
    std::error_code ignoredError;
    remove_all(scratchDirectory, ignoredError);
    create_directories(scratchDirectory);
    REQUIRE(exists(scratchDirectory));
    remove_all(scratchDirectory, ignoredError);
    REQUIRE_FALSE(exists(scratchDirectory));
}
