// The unit tests for the KnownFailurePatterns class.
//
// The class exists so that a person in a crisis is told what happened in words they
// can act on. The tests therefore check that the specific story the specification
// tells is recognised by name, and that the tool says out loud when data is gone
// rather than leaving a user to keep trying commands that cannot work.
#include <string>
// Provides the SQLite result code constants the classifier branches on.
#include <sqlite3.h>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the class under test.
#include "AgentDbDoctor/Recovery/KnownFailurePatterns.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Recovery;
using namespace AgentDbDoctor::FileSystem;
namespace {
// The SQLite result code the specification names, which is the read failure that a
// half finished compression produces. It is the primary read error with the
// extended code one added, which is 266.
constexpr int SQLITE_IOERR_READ_CODE=266;
FileMetadata buildFileMetadata(long long logicalSizeBytes, long long allocatedSizeBytes) {
    FileMetadata metadata;
    metadata.exists=true;
    metadata.logicalSizeBytes=logicalSizeBytes;
    metadata.allocatedSizeBytes=allocatedSizeBytes;
    return metadata;
}
FileLockStatus buildFreeLockStatus() {
    FileLockStatus status;
    status.isLocked=false;
    return status;
}
FileLockStatus buildBusyLockStatus() {
    FileLockStatus status;
    status.isLocked=true;
    status.explanation="Another program has this database open.";
    return status;
}
DiskSpace buildDiskSpace(long long availableBytes) {
    DiskSpace space;
    space.isValid=true;
    space.totalBytes=availableBytes*4;
    space.availableBytes=availableBytes;
    return space;
}
}
TEST_CASE("a plain healthy file matches nothing in particular", "[patterns]") {
    // A file occupying slightly more than its logical size is completely normal,
    // because a file system allocates space in whole clusters.
    const FileMetadata metadata=buildFileMetadata(1000, 1050);
    const FailureMatch match=KnownFailurePatterns::classify(
        0,
        metadata,
        buildFreeLockStatus(),
        buildDiskSpace(1000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.severity==SeverityLevel::INFORMATION);
    // Nothing is wrong, so nothing is claimed to be unrecoverable.
    REQUIRE_FALSE(match.dataIsUnrecoverable);
}
TEST_CASE("a compressed database is recognised and explained", "[patterns]") {
    // A file occupying far less space than its contents is the signature of a
    // compressed or sparse database, which is the state the specification warns
    // about at length.
    const FileMetadata metadata=buildFileMetadata(1000000, 100000);
    REQUIRE(KnownFailurePatterns::looksCompressedOrSparse(metadata));
    const FailureMatch match=KnownFailurePatterns::classify(
        SQLITE_READONLY,
        metadata,
        buildFreeLockStatus(),
        buildDiskSpace(1000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="compressed-while-open");
    REQUIRE(match.severity==SeverityLevel::URGENT);
    // The explanation must name the reason, because "do not compress it" is useless
    // advice without knowing why.
    REQUIRE(match.plainExplanation.find("compress")!=string::npos);
    REQUIRE(match.recoverableByThisTool);
}
TEST_CASE("an interrupted compression is recognised and called final", "[patterns]") {
    // This is the exact story the specification tells. The signature is a read
    // failure on a file whose allocated size disagrees with its own.
    const FileMetadata metadata=buildFileMetadata(11000000000LL, 124000000000LL);
    REQUIRE(KnownFailurePatterns::hasAllocatedSizeMismatch(metadata, 124000000000LL));
    const FailureMatch match=KnownFailurePatterns::classify(
        SQLITE_IOERR_READ_CODE,
        metadata,
        buildFreeLockStatus(),
        buildDiskSpace(36000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="interrupted-compression");
    // The most severe level is the one the tool uses to say it has reached the end
    // of what it can do, and this is that case.
    REQUIRE(match.severity==SeverityLevel::BEYOND_RECOVERY);
    REQUIRE(match.dataIsUnrecoverable);
    // The message has to be honest about what is gone, and helpful about what is not.
    REQUIRE(match.plainExplanation.find("gone")!=string::npos);
    REQUIRE(match.plainExplanation.find("still intact")!=string::npos);
}
TEST_CASE("an unreadable file is named as such", "[patterns]") {
    const FileMetadata metadata=buildFileMetadata(1000, 1050);
    const FailureMatch match=KnownFailurePatterns::classify(
        SQLITE_IOERR_READ_CODE,
        metadata,
        buildFreeLockStatus(),
        buildDiskSpace(1000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="unreadable-file");
    // A plain read failure is not always a lost file, but the tool cannot tell, so
    // it says the data may be gone rather than promising otherwise.
    REQUIRE(match.dataIsUnrecoverable);
    // The first thing to do is check the drive, because a failing drive is the most
    // common cause and every further read makes it worse.
    REQUIRE(match.plainExplanation.find("drive")!=string::npos);
}
TEST_CASE("a file that is not a database is named as such", "[patterns]") {
    const FileMetadata metadata=buildFileMetadata(1000, 1050);
    const FailureMatch match=KnownFailurePatterns::classify(
        SQLITE_NOTADB,
        metadata,
        buildFreeLockStatus(),
        buildDiskSpace(1000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="not-a-database");
    REQUIRE(match.severity==SeverityLevel::URGENT);
}
TEST_CASE("an interrupted write is recoverable", "[patterns]") {
    // A journal waiting to be replayed is the best case there is, because SQLite
    // fixes it on the next open.
    FileLockStatus statusWithJournal=buildFreeLockStatus();
    statusWithJournal.hasRollbackJournal=true;
    const FileMetadata metadata=buildFileMetadata(1000, 1050);
    const FailureMatch match=KnownFailurePatterns::classify(
        0,
        metadata,
        statusWithJournal,
        buildDiskSpace(1000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="interrupted-write");
    REQUIRE(match.recoverableByThisTool);
    REQUIRE_FALSE(match.dataIsUnrecoverable);
}
TEST_CASE("a database another program holds is not treated as damaged", "[patterns]") {
    const FileMetadata metadata=buildFileMetadata(1000, 1050);
    const FailureMatch match=KnownFailurePatterns::classify(
        0,
        metadata,
        buildBusyLockStatus(),
        buildDiskSpace(1000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="in-use-by-another-program");
    // Nothing is wrong, so the tool must not claim anything is unrecoverable.
    REQUIRE_FALSE(match.dataIsUnrecoverable);
    // The explanation repeats the reason the lock probe gave, so the user is told
    // what to close rather than only that something is open.
    REQUIRE(match.plainExplanation.find("Another program")!=string::npos);
}
TEST_CASE("structural damage is recoverable", "[patterns]") {
    const FileMetadata metadata=buildFileMetadata(1000, 1050);
    const FailureMatch match=KnownFailurePatterns::classify(
        SQLITE_CORRUPT,
        metadata,
        buildFreeLockStatus(),
        buildDiskSpace(1000000000LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="structurally-damaged");
    // Structural damage is usually in an index, and the conversation data usually
    // survives, so this is not the same as unreadable.
    REQUIRE(match.recoverableByThisTool);
    REQUIRE_FALSE(match.dataIsUnrecoverable);
}
TEST_CASE("a nearly full drive is reported before anything else", "[patterns]") {
    const FileMetadata metadata=buildFileMetadata(1000, 1050);
    const FailureMatch match=KnownFailurePatterns::classify(
        0,
        metadata,
        buildFreeLockStatus(),
        buildDiskSpace(1024LL)
    );
    REQUIRE(match.matched);
    REQUIRE(match.patternName=="disk-full");
    // A nearly full drive is the most common cause of a damaged agent database, and
    // saying so is more useful than treating the damage as the whole story.
    REQUIRE(match.severity==SeverityLevel::URGENT);
    REQUIRE(match.plainExplanation.find("free space")!=string::npos);
}
TEST_CASE("a compressed file is not mistaken for a healthy one", "[patterns]") {
    const FileMetadata healthy=buildFileMetadata(1000000, 1000000);
    const FileMetadata compressed=buildFileMetadata(1000000, 100000);
    // The two must be told apart, or the tool would report a compressed database as
    // being in good shape and the user would never find out it cannot be written to.
    REQUIRE_FALSE(KnownFailurePatterns::looksCompressedOrSparse(healthy));
    REQUIRE(KnownFailurePatterns::looksCompressedOrSparse(compressed));
}
TEST_CASE("every pattern the class can produce is named in its list", "[patterns]") {
    const vector<string> patternNames=KnownFailurePatterns::allPatternNames();
    // The list is the documented set, and a pattern that is produced but not listed
    // would be invisible to the tests and to the diagnose command.
    REQUIRE(patternNames.size()==9);
    REQUIRE(patternNames[0]=="compressed-while-open");
    REQUIRE(patternNames[1]=="unreadable-file");
    REQUIRE(patternNames[2]=="interrupted-compression");
}
