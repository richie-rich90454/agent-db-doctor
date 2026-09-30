// The unit tests for the Result and Status types.
//
// These types carry ordinary failures without an exception, so the properties worth
// testing are the ones a caller depends on: a success never reports a message, a
// failure never reports a value, and neither throws.
#include <string>
// Provides `optional`, the container a failed result leaves empty.
#include <optional>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the Result and Status types under test.
#include "AgentDbDoctor/Common/Result.hpp"
using namespace std;
using namespace AgentDbDoctor::Common;
TEST_CASE("a successful status says so and carries no message", "[result][status]") {
    const Status succeeded=Status::success();
    REQUIRE(succeeded.isSuccess());
    REQUIRE_FALSE(succeeded.isFailure());
    REQUIRE(succeeded.errorMessage().empty());
}
TEST_CASE("a failed status keeps the sentence it was given", "[result][status]") {
    const Status failure=Status::failure("The file does not exist. Nothing was changed.");
    REQUIRE(failure.isFailure());
    REQUIRE_FALSE(failure.isSuccess());
    REQUIRE(failure.errorMessage()=="The file does not exist. Nothing was changed.");
}
TEST_CASE("a successful result returns its value", "[result]") {
    const Result<int> succeeded=Result<int>::succeeded(42);
    REQUIRE(succeeded.isSuccess());
    REQUIRE_FALSE(succeeded.isFailure());
    REQUIRE(succeeded.get()==42);
    // A success has no message, and reading one must not invent one.
    REQUIRE(succeeded.errorMessage().empty());
}
TEST_CASE("a failed result keeps its sentence and refuses a value", "[result]") {
    const Result<int> failure=Result<int>::failure("The query could not be run.");
    REQUIRE(failure.isFailure());
    REQUIRE(failure.errorMessage()=="The query could not be run.");
    // The optional is empty, so a caller that checks first is safe.
    REQUIRE_FALSE(failure.tryGet().has_value());
}
TEST_CASE("a value type result carries its value through the optional", "[result]") {
    const Result<string> succeeded=Result<string>::succeeded("a value");
    const optional<string> present=succeeded.tryGet();
    REQUIRE(present.has_value());
    REQUIRE(present.value()=="a value");
}
TEST_CASE("results are independent of one another", "[result]") {
    // Two results built from the same type must not share storage, or setting one
    // would change the other.
    Result<int> first=Result<int>::succeeded(1);
    Result<int> second=Result<int>::succeeded(2);
    REQUIRE(first.get()==1);
    REQUIRE(second.get()==2);
}
TEST_CASE("a failed result offers no value to read", "[result]") {
    const Result<string> failure=Result<string>::failure("Something specific went wrong.");
    // The optional is empty, which is the safe way for a caller to ask whether there
    // is a value. Reading the value directly would throw, so a caller that checked
    // first is the one that stays in control.
    REQUIRE_FALSE(failure.tryGet().has_value());
    REQUIRE(failure.errorMessage()=="Something specific went wrong.");
}
