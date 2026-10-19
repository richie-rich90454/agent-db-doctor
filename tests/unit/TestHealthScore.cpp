// The unit tests for the health score.
//
// The score is a formula, so the tests check the formula at its extremes and at
// the boundaries of each rule, using the same public constants the formula uses.
// A test written against a copied number would pass while the documentation and
// the code drifted apart, which is the one failure a score must not have.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the class under test and the constants of its formula.
#include "AgentDbDoctor/Doctor/HealthScore.hpp"
// Provides the DiagnosisReport value type the score is computed from.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
using namespace std;
using namespace AgentDbDoctor::Doctor;
namespace {
// A report for a sound database: the file exists, the check passed, and nothing
// in it is worth a warning. Every test starts from this and changes one thing, so
// each test proves exactly one rule.
DiagnosisReport buildSoundReport() {
    DiagnosisReport report("/data/opencode.db");
    report.setFileExists(true);
    report.setIntegrityStatus(IntegrityStatus::INTACT);
    report.setPageSizeBytes(4096);
    report.setPageCount(100);
    report.setFreePageCount(0);
    return report;
}
}
TEST_CASE("a sound database scores full marks", "[health]") {
    const DiagnosisReport report=buildSoundReport();
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100);
}
TEST_CASE("damage costs most of the score", "[health]") {
    // Each of the four states in which the rest of the report cannot be relied on
    // costs the same, because to a user they all mean "do not trust anything else
    // here yet".
    for (const IntegrityStatus status : {
        IntegrityStatus::STRUCTURALLY_DAMAGED,
        IntegrityStatus::UNREADABLE,
        IntegrityStatus::NOT_A_DATABASE,
        IntegrityStatus::PROBLEM_REPORTED
    }) {
        DiagnosisReport report=buildSoundReport();
        report.setIntegrityStatus(status);
        REQUIRE(HealthScore::scoreFor(report, 0.0)==100 - HealthScore::DAMAGE_LOSS);
    }
}
TEST_CASE("a check that was not run costs nothing", "[health]") {
    // A skipped integrity check is not evidence of damage; the report says in
    // words that it was skipped, and the score must not punish it.
    DiagnosisReport report=buildSoundReport();
    report.setIntegrityStatus(IntegrityStatus::NOT_CHECKED);
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100);
}
TEST_CASE("warnings cost five points each up to a cap", "[health]") {
    DiagnosisReport report=buildSoundReport();
    report.addWarning("the first thing worth knowing");
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100 - HealthScore::WARNING_LOSS);
    for (int warningIndex=0; warningIndex<3; ++warningIndex) {
        report.addWarning("another thing worth knowing");
    }
    // Four warnings reach the cap exactly.
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100 - HealthScore::MAXIMUM_WARNING_LOSS);
    // A talkative database cannot be pushed below the cap, because messages alone
    // must not outweigh damage or growth.
    for (int warningIndex=0; warningIndex<6; ++warningIndex) {
        report.addWarning("one more thing worth knowing");
    }
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100 - HealthScore::MAXIMUM_WARNING_LOSS);
}
TEST_CASE("free pages cost in proportion to how many there are", "[health]") {
    DiagnosisReport report=buildSoundReport();
    // A quarter of the pages free is the boundary the rule allows, so it costs
    // nothing: many healthy databases sit exactly there after ordinary use.
    report.setFreePageCount(25);
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100);
    // Half the pages free costs a third of the maximum loss, which rounds to 97.
    report.setFreePageCount(50);
    REQUIRE(HealthScore::scoreFor(report, 0.0)==97);
    // Every page free costs the whole of it.
    report.setFreePageCount(100);
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100 - HealthScore::MAXIMUM_FREE_PAGE_LOSS);
}
TEST_CASE("growth costs in proportion to the reference rate", "[health]") {
    const DiagnosisReport report=buildSoundReport();
    // Exactly the reference rate, one gigabyte a day, costs the whole loss.
    REQUIRE(HealthScore::scoreFor(report, HealthScore::REFERENCE_GROWTH_BYTES_PER_DAY)
        ==100 - HealthScore::MAXIMUM_GROWTH_LOSS);
    // Twice the reference rate cannot cost more than the whole loss.
    REQUIRE(HealthScore::scoreFor(report, 2.0 * HealthScore::REFERENCE_GROWTH_BYTES_PER_DAY)
        ==100 - HealthScore::MAXIMUM_GROWTH_LOSS);
    // Half the reference rate costs half of it, rounded to the nearest point.
    REQUIRE(HealthScore::scoreFor(report, 0.5 * HealthScore::REFERENCE_GROWTH_BYTES_PER_DAY)==93);
    // A database that is not growing, or shrinking, loses nothing.
    REQUIRE(HealthScore::scoreFor(report, 0.0)==100);
    REQUIRE(HealthScore::scoreFor(report, -500000000.0)==100);
}
TEST_CASE("the losses add up and cannot go below zero", "[health]") {
    DiagnosisReport report=buildSoundReport();
    report.setIntegrityStatus(IntegrityStatus::STRUCTURALLY_DAMAGED);
    report.addWarning("first");
    report.addWarning("second");
    // Damage and two warnings: sixty plus ten.
    REQUIRE(HealthScore::scoreFor(report, 0.0)==30);
    // Everything wrong at once subtracts more than a hundred points, and the
    // score is clamped rather than going negative.
    for (int warningIndex=0; warningIndex<8; ++warningIndex) {
        report.addWarning("one more");
    }
    report.setFreePageCount(100);
    REQUIRE(HealthScore::scoreFor(report, HealthScore::REFERENCE_GROWTH_BYTES_PER_DAY)==0);
}
TEST_CASE("a missing file and a folder have no score", "[health]") {
    DiagnosisReport missingReport("/data/not-there.db");
    missingReport.setFileExists(false);
    // A file that does not exist is not a bad database; there is simply nothing
    // to score, and the report words the difference.
    REQUIRE(HealthScore::scoreFor(missingReport, 0.0)==HealthScore::UNKNOWN_SCORE);
    DiagnosisReport folderReport("/data/notes");
    folderReport.setFileExists(true);
    folderReport.setIsDirectory(true);
    folderReport.setIntegrityStatus(IntegrityStatus::IS_A_FOLDER);
    // Several supported agents keep plain text in a folder, which is not a fault
    // and must not be scored as one.
    REQUIRE(HealthScore::scoreFor(folderReport, 0.0)==HealthScore::UNKNOWN_SCORE);
}
