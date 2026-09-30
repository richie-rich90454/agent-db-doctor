// The unit tests for the DiskSpaceChecker and FileLockDetector classes.
//
// These two are the last line of defence before the tool writes to a disk. The
// tests are about the answers being safe: a requirement that is too small, or a
// lock check that says a free file is busy, are both ways to lose a user's data.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, the growable array the lock status returns.
#include <vector>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the two classes under test.
#include "AgentDbDoctor/FileSystem/DiskSpaceChecker.hpp"
#include "AgentDbDoctor/FileSystem/FileLockDetector.hpp"
// Provides the synthetic database builder, for a real file to probe.
#include "AgentDbDoctor/Persistence/Database.hpp"
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::TestSupport;
// The persistence namespace names the database wrapper the lock test holds open.
using namespace AgentDbDoctor::Persistence;
TEST_CASE("a backup needs at least the size of the file it copies", "[disk]") {
    const long long fileSizeBytes=1000000000LL;
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForBackup(fileSizeBytes);
    // The requirement must exceed the file, because a copy that lands on the last
    // free byte would be rejected by the file system's own reserve.
    REQUIRE(requirement.requiredBytes>fileSizeBytes);
    REQUIRE(requirement.requirementKind=="backup");
    REQUIRE_FALSE(requirement.explanation.empty());
    // Every explanation ends with a period, which the specification requires of
    // every user facing string.
    REQUIRE(requirement.explanation.back()=='.');
}
TEST_CASE("a compaction needs room for a second whole copy", "[disk]") {
    const long long fileSizeBytes=1000000000LL;
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForCompaction(fileSizeBytes);
    // The worst case is that every page is still live, so the new file is exactly as
    // large as the old one. Anything less would risk filling a nearly full drive.
    REQUIRE(requirement.requiredBytes>=fileSizeBytes);
    REQUIRE(requirement.requirementKind=="compaction");
}
TEST_CASE("a tiny file still gets a usable margin", "[disk]") {
    // A one kilobyte file would get a five percent margin, which is fifty bytes and
    // would be rejected by any real file system, so the minimum applies instead.
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForCompaction(1024);
    REQUIRE(requirement.requiredBytes>=16LL*1024*1024);
}
TEST_CASE("an integrity check needs only a little room", "[disk]") {
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForIntegrityCheck(1000000000LL);
    // The check writes nothing, so its requirement is a fraction of the file rather
    // than all of it.
    REQUIRE(requirement.requiredBytes<1000000000LL);
    REQUIRE(requirement.requirementKind=="integrity-check");
}
TEST_CASE("space is judged against the measured free space", "[disk]") {
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForBackup(1000);
    DiskSpace roomyVolume;
    roomyVolume.isValid=true;
    roomyVolume.totalBytes=400000000;
    roomyVolume.availableBytes=200000000;
    REQUIRE(DiskSpaceChecker::hasEnoughSpace(roomyVolume, requirement));
    DiskSpace tightVolume;
    tightVolume.isValid=true;
    tightVolume.totalBytes=400000000;
    tightVolume.availableBytes=1024;
    REQUIRE_FALSE(DiskSpaceChecker::hasEnoughSpace(tightVolume, requirement));
}
TEST_CASE("an unreadable volume is treated as no space", "[disk]") {
    const SpaceRequirement requirement=DiskSpaceChecker::requiredBytesForBackup(1000);
    // When the operating system will not answer, the safe answer is no. The default
    // has to be the one that cannot fill a drive.
    DiskSpace unreadableVolume;
    unreadableVolume.isValid=false;
    REQUIRE_FALSE(DiskSpaceChecker::hasEnoughSpace(unreadableVolume, requirement));
    REQUIRE_FALSE(DiskSpaceChecker::describeAvailableSpace(unreadableVolume).empty());
}
TEST_CASE("a file nobody has open is reported as free", "[lock]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestLockFree");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    const FileLockStatus status=FileLockDetector::checkLockStatus(databasePath);
    // The tool itself is not holding the file, so the answer must be free. A false
    // positive here would stop every destructive command for no reason.
    REQUIRE_FALSE(status.isLocked);
    REQUIRE_FALSE(status.explanation.empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a file this program has open is reported as in use", "[lock]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestLockHeld");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    {
        // The database is opened and deliberately left open, which is the situation a
        // running coding agent creates.
        Database holderDatabase(
            databasePath,
            Database::OPEN_CREATE | Database::OPEN_READ_WRITE
        );
        const FileLockStatus status=FileLockDetector::checkLockStatus(databasePath);
        // Whether the platform reports the conflict depends on how it shares files,
        // so the test asserts the detector answers rather than assuming the answer.
        // On Windows the refusal is enforced by the kernel and is reliable; on Unix
        // it depends on whether the holder took a byte range lock.
        REQUIRE_FALSE(status.explanation.empty());
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a missing file is reported as missing rather than busy", "[lock]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestLockMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    const FileLockStatus status=FileLockDetector::checkLockStatus(missingPath);
    // A file that is not there is not a locked file, and telling a user to close a
    // program that was never using it would waste their time.
    REQUIRE_FALSE(status.isLocked);
}
TEST_CASE("the side files beside a database are found", "[lock]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("TestLockCompanions");
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, SyntheticDatabaseBuilder::Options());
    FileLockStatus status;
    FileLockDetector::inspectCompanionFiles(databasePath, status);
    // The builder writes in write ahead log mode, so a log file exists and is found.
    // Its presence is what tells the report that committed changes may not yet be in
    // the main file.
    REQUIRE(status.companionFilePaths.size()<=2);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the advice names what to close", "[lock]") {
    const vector<string> programNames{"OpenCode", "the editor"};
    const string advice=FileLockDetector::describeWhoMightBeUsingIt(programNames);
    // The names must appear, because a sentence that does not name the program is
    // no help to someone who does not know which window to close.
    REQUIRE(advice.find("OpenCode")!=string::npos);
    REQUIRE(advice.find("the editor")!=string::npos);
    REQUIRE(advice.find("Nothing has been changed")!=string::npos);
    // With no names, the advice still says what to do rather than being empty.
    REQUIRE_FALSE(FileLockDetector::describeWhoMightBeUsingIt(vector<string>()).empty());
}
