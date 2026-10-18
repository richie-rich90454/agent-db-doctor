// The unit tests for the schema fingerprint.
//
// The fingerprint exists for one reason: an agent that changes the layout of its
// database can make every number this tool reports quietly wrong. These tests check
// that the fingerprint notices a layout change, ignores ordinary data changes, and
// that a stored fingerprint that no longer matches turns into a warning on the next
// diagnose.
#include <string>
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the class under test.
#include "AgentDbDoctor/Doctor/SchemaFingerprint.hpp"
// Provides the doctor, whose warning the last test checks.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
// Provides the DiagnosisReport the doctor returns.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
// Provides the database wrapper, so the test can change a schema itself.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Builds a small database and returns its path.
string buildSmallDatabase(const string& testName) {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath(testName);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=4;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    return databasePath;
}
// True when any warning in the report mentions the given fragment.
bool reportMentions(const DiagnosisReport& report, const string& fragment) {
    for (const string& warning : report.getWarnings()) {
        if (warning.find(fragment)!=string::npos) {
            return true;
        }
    }
    return false;
}
}
TEST_CASE("the fingerprint changes when the schema changes", "[fingerprint]") {
    const string databasePath=buildSmallDatabase("FingerprintChanges");
    string fingerprintBefore;
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        fingerprintBefore=SchemaFingerprint::fingerprintOf(database);
    }
    // A fingerprint of a real database is sixteen hexadecimal digits.
    REQUIRE(fingerprintBefore.size()==16);
    // Ordinary data changes do not change the schema and must not change the
    // fingerprint, or every diagnose would warn about nothing.
    {
        Database database(databasePath, Database::OPEN_READ_WRITE);
        database.execute("INSERT INTO session (id, time_created, time_updated) VALUES ('ses_extra', 1, 2)");
    }
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(SchemaFingerprint::fingerprintOf(database)==fingerprintBefore);
    }
    // A layout change does change it.
    {
        Database database(databasePath, Database::OPEN_READ_WRITE);
        database.execute("ALTER TABLE session ADD COLUMN title TEXT");
    }
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(SchemaFingerprint::fingerprintOf(database)!=fingerprintBefore);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a stored fingerprint survives a round trip through the store", "[fingerprint]") {
    const string storePath=SyntheticDatabaseBuilder::temporaryDatabasePath("FingerprintStore") + ".tsv";
    std::filesystem::remove(storePath);
    SchemaFingerprint::setStorePathForTests(storePath);
    // Nothing stored yet means nothing to compare, which is not an error.
    REQUIRE_FALSE(SchemaFingerprint::storedFingerprintFor("/tmp/never-seen.db").has_value());
    REQUIRE(SchemaFingerprint::storeFingerprint("/tmp/one.db", "0123456789abcdef"));
    REQUIRE(SchemaFingerprint::storeFingerprint("/tmp/two.db", "fedcba9876543210"));
    REQUIRE(SchemaFingerprint::storedFingerprintFor("/tmp/one.db").value_or("")=="0123456789abcdef");
    REQUIRE(SchemaFingerprint::storedFingerprintFor("/tmp/two.db").value_or("")=="fedcba9876543210");
    // Storing the same path again replaces the value rather than adding a line.
    REQUIRE(SchemaFingerprint::storeFingerprint("/tmp/one.db", "1111111111111111"));
    REQUIRE(SchemaFingerprint::storedFingerprintFor("/tmp/one.db").value_or("")=="1111111111111111");
    std::filesystem::remove(storePath);
}
TEST_CASE("diagnose warns when the stored layout no longer matches", "[fingerprint]") {
    const string databasePath=buildSmallDatabase("FingerprintWarns");
    const string storePath=SyntheticDatabaseBuilder::temporaryDatabasePath("FingerprintWarnStore") + ".tsv";
    std::filesystem::remove(storePath);
    SchemaFingerprint::setStorePathForTests(storePath);
    // The watchdog's view of this database is recorded first.
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        SchemaFingerprint::storeFingerprint(databasePath, SchemaFingerprint::fingerprintOf(database));
    }
    AgentDatabaseDoctor doctor;
    // Nothing has changed, so nothing is said.
    {
        const DiagnosisReport report=doctor.diagnoseDatabase(databasePath, "OpenCode", false);
        REQUIRE_FALSE(reportMentions(report, "layout of this database has changed"));
    }
    // The agent "changes its layout", and the next diagnose says so.
    {
        Database database(databasePath, Database::OPEN_READ_WRITE);
        database.execute("ALTER TABLE part ADD COLUMN extra TEXT");
    }
    {
        const DiagnosisReport report=doctor.diagnoseDatabase(databasePath, "OpenCode", false);
        REQUIRE(reportMentions(report, "layout of this database has changed"));
    }
    // Once the watchdog records the new layout, the warning stops.
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        SchemaFingerprint::storeFingerprint(databasePath, SchemaFingerprint::fingerprintOf(database));
    }
    {
        const DiagnosisReport report=doctor.diagnoseDatabase(databasePath, "OpenCode", false);
        REQUIRE_FALSE(reportMentions(report, "layout of this database has changed"));
    }
    std::filesystem::remove(storePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
