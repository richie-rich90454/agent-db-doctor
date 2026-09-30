// The unit tests for the HumanReadableFormatting class.
//
// Every number the tool shows a user passes through this class, so the tests are
// about what a person reads: that a size is spelled with a unit a person knows,
// that a count has separators, and that a duration picks a unit that makes the
// number easy to compare.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the class under test.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor::Presentation;
TEST_CASE("a byte count is spelled with a unit a person knows", "[formatting]") {
    REQUIRE(HumanReadableFormatting::formatBytes(512)=="512 bytes");
    REQUIRE(HumanReadableFormatting::formatBytes(2048)=="2.0 KB");
    REQUIRE(HumanReadableFormatting::formatBytes(5LL*1024*1024)=="5.0 MB");
    REQUIRE(HumanReadableFormatting::formatBytes(3LL*1024*1024*1024)=="3.0 GB");
}
TEST_CASE("a negative or nonsensical size reads as zero", "[formatting]") {
    // A negative size should never reach this class, and showing one would be more
    // confusing than showing none.
    REQUIRE(HumanReadableFormatting::formatBytes(-1)=="0 bytes");
    REQUIRE(HumanReadableFormatting::formatBytes(0)=="0 bytes");
}
TEST_CASE("a count has separators so it can be read", "[formatting]") {
    REQUIRE(HumanReadableFormatting::formatCount(0)=="0");
    REQUIRE(HumanReadableFormatting::formatCount(999)=="999");
    REQUIRE(HumanReadableFormatting::formatCount(1000)=="1,000");
    REQUIRE(HumanReadableFormatting::formatCount(1234567)=="1,234,567");
    // A negative count keeps the sign in front of the digits rather than beside the
    // first group.
    REQUIRE(HumanReadableFormatting::formatCount(-1234567)=="-1,234,567");
}
TEST_CASE("a duration picks the unit that makes the number readable", "[formatting]") {
    REQUIRE(HumanReadableFormatting::formatDuration(500)=="500 nanoseconds");
    REQUIRE(HumanReadableFormatting::formatDuration(5000)=="5 microseconds");
    REQUIRE(HumanReadableFormatting::formatDuration(3800000)=="3 milliseconds");
    REQUIRE(HumanReadableFormatting::formatDuration(1400000000)=="1.4 seconds");
    REQUIRE(HumanReadableFormatting::formatDuration(120000000000LL)=="2 minutes");
}
TEST_CASE("a duration of unknown length says so rather than guessing", "[formatting]") {
    REQUIRE(HumanReadableFormatting::formatDuration(-1)=="an unknown amount of time");
}
TEST_CASE("a proportion is shown as a percentage", "[formatting]") {
    REQUIRE(HumanReadableFormatting::formatPercentage(0.0, 0)=="0%");
    REQUIRE(HumanReadableFormatting::formatPercentage(0.5, 0)=="50%");
    REQUIRE(HumanReadableFormatting::formatPercentage(0.1234, 1)=="12.3%");
    // A value outside the range is clamped, so a rounding error cannot produce a
    // percentage above one hundred.
    REQUIRE(HumanReadableFormatting::formatPercentage(1.5, 0)=="100%");
    REQUIRE(HumanReadableFormatting::formatPercentage(-0.5, 0)=="0%");
}
TEST_CASE("a share of a whole reads as a phrase", "[formatting]") {
    REQUIRE(HumanReadableFormatting::describeShareOfWhole(0, 100)=="none of it");
    REQUIRE(HumanReadableFormatting::describeShareOfWhole(100, 100)=="all of it");
    REQUIRE(HumanReadableFormatting::describeShareOfWhole(50, 100)=="50.0% of it");
    // A whole of zero has no share to describe, so the phrase says so.
    REQUIRE(HumanReadableFormatting::describeShareOfWhole(5, 0)=="the whole file");
}
TEST_CASE("a timestamp is readable and a relative one picks the largest unit", "[formatting]") {
    const long long oneSecond=1000LL;
    const long long oneMinute=60LL*oneSecond;
    const long long oneHour=60LL*oneMinute;
    const long long oneDay=24LL*oneHour;
    const long long oneWeek=7LL*oneDay;
    const long long now=1700000000000LL;
    REQUIRE(HumanReadableFormatting::formatRelativeTime(now-5LL*oneSecond, now)=="5 seconds ago");
    REQUIRE(HumanReadableFormatting::formatRelativeTime(now-5LL*oneMinute, now)=="5 minutes ago");
    REQUIRE(HumanReadableFormatting::formatRelativeTime(now-5LL*oneHour, now)=="5 hours ago");
    REQUIRE(HumanReadableFormatting::formatRelativeTime(now-3LL*oneDay, now)=="3 days ago");
    REQUIRE(HumanReadableFormatting::formatRelativeTime(now-2LL*oneWeek, now)=="2 weeks ago");
    // A time in the future is described as such rather than as a negative age.
    REQUIRE(HumanReadableFormatting::formatRelativeTime(now+oneDay, now)=="in the future");
}
TEST_CASE("a timestamp renders as a date and a time", "[formatting]") {
    const string rendered=HumanReadableFormatting::formatTimestamp(1700000000000LL);
    // The exact value depends on the machine's time zone, so the test checks the
    // shape rather than the text: four digits, a dash, two digits, and so on.
    REQUIRE(rendered.size()==19);
    REQUIRE(rendered[4]=='-');
    REQUIRE(rendered[7]=='-');
    REQUIRE(rendered[10]==' ');
    REQUIRE(rendered[13]==':');
}
TEST_CASE("a precise size keeps two decimal places", "[formatting]") {
    REQUIRE(HumanReadableFormatting::formatBytesPrecisely(3LL*1024*1024*1024)=="3.00 GB");
}
