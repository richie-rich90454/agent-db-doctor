// The implementation of the KnownFailurePatterns class.
//
// The patterns are checked most specific first, so that a database which matches
// two patterns is reported as the more specific one. The order matters: the
// interrupted compression case also looks like an unreadable file, and the user
// needs to hear about the compression.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the raw SQLite result code constants.
#include <sqlite3.h>
// Provides the KnownFailurePatterns class declaration.
#include "AgentDbDoctor/Recovery/KnownFailurePatterns.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Recovery {
// The names of the patterns. They are constants because they appear in more than
// one place and a typo in one of them would break a test.
static constexpr const char* PATTERN_COMPRESSED_WHILE_OPEN="compressed-while-open";
static constexpr const char* PATTERN_UNREADABLE_FILE="unreadable-file";
static constexpr const char* PATTERN_INTERRUPTED_COMPRESSION="interrupted-compression";
static constexpr const char* PATTERN_NOT_A_DATABASE="not-a-database";
static constexpr const char* PATTERN_INTERRUPTED_WRITE="interrupted-write";
static constexpr const char* PATTERN_IN_USE="in-use-by-another-program";
static constexpr const char* PATTERN_DISK_FULL="disk-full";
static constexpr const char* PATTERN_COMPRESSED_OR_SPARSE="compressed-or-sparse";
static constexpr const char* PATTERN_STRUCTURALLY_DAMAGED="structurally-damaged";
// The ratio by which the allocated size must fall below the logical size before a
// file is called compressed. A healthy database occupies slightly more than its
// logical size because of cluster rounding, so the comparison has to allow for
// that before it can mean anything.
static constexpr double COMPRESSION_SIGNATURE_RATIO=0.9;
// How much larger the allocated size must be than the declared size, as a
// fraction, before the mismatch is called out. A small difference is normal,
// because a file being written has pages the header has not recorded yet.
static constexpr double SIZE_MISMATCH_TOLERANCE=0.05;
// The number of free bytes below which the drive is called nearly full. It matches
// the smallest margin the space checker ever adds, so the warning this produces
// and the refusal that space checker makes always agree with each other.
static constexpr long long SpaceRequirementThresholdForWarning=16LL * 1024LL * 1024LL;
FailureMatch KnownFailurePatterns::noMatch() {
    return FailureMatch();
}
FailureMatch KnownFailurePatterns::buildMatch(
    string patternName,
    SeverityLevel severity,
    string plainExplanation,
    bool isRecoverable,
    bool isUnrecoverable
) {
    FailureMatch match;
    match.patternName=move(patternName);
    match.matched=true;
    match.severity=severity;
    match.plainExplanation=move(plainExplanation);
    match.recoverableByThisTool=isRecoverable;
    match.dataIsUnrecoverable=isUnrecoverable;
    return match;
}
bool KnownFailurePatterns::looksCompressedOrSparse(const FileMetadata& fileMetadata) {
    if (fileMetadata.logicalSizeBytes<=0) {
        return false;
    }
    // A compressed or sparse file occupies materially less space than its logical
    // size. A healthy file occupies slightly more, because a file system allocates
    // space in clusters and the last cluster of a file is usually only partly
    // used. So the test is for less than ninety percent, not for less than the
    // logical size.
    const double occupiedRatio=static_cast<double>(fileMetadata.allocatedSizeBytes)
        / static_cast<double>(fileMetadata.logicalSizeBytes);
    return occupiedRatio<COMPRESSION_SIGNATURE_RATIO;
}
bool KnownFailurePatterns::hasAllocatedSizeMismatch(
    const FileMetadata& fileMetadata,
    long long declaredFileSizeBytes
) {
    if (fileMetadata.allocatedSizeBytes<=0) {
        return false;
    }
    // The first comparison is the one that catches the story in the
    // specification: the file system still holds clusters for a file that no
    // longer describes itself, so it reports far more space than the file needs.
    if (fileMetadata.logicalSizeBytes>0
        && static_cast<double>(fileMetadata.allocatedSizeBytes)
            > static_cast<double>(fileMetadata.logicalSizeBytes) * (1.0 + SIZE_MISMATCH_TOLERANCE)) {
        return true;
    }
    // The second is the same disagreement measured from SQLite's side: the page
    // count in the header describes a larger file than the file system sees.
    if (declaredFileSizeBytes<=0) {
        return false;
    }
    return static_cast<double>(fileMetadata.allocatedSizeBytes)
        > static_cast<double>(declaredFileSizeBytes) * (1.0 + SIZE_MISMATCH_TOLERANCE);
}
FailureMatch KnownFailurePatterns::classify(
    int sqliteResultCode,
    const FileMetadata& fileMetadata,
    const FileLockStatus& lockStatus,
    const DiskSpace& availableSpace
) {
    // The first pattern is the one the specification tells the story about: a
    // database that was compressed while it was open, and whose compression was
    // interrupted. The signature is an operating system read failure on a file
    // whose allocated size does not match the size the file system reports.
    if (sqliteResultCode==SQLITE_IOERR_READ
        && hasAllocatedSizeMismatch(fileMetadata, fileMetadata.logicalSizeBytes)) {
        return buildMatch(
            string(PATTERN_INTERRUPTED_COMPRESSION),
            SeverityLevel::BEYOND_RECOVERY,
            string(
                "This file was compressed while the database was using it, and the compression did "
                "not finish. The file system still has clusters for the file that the file itself "
                "no longer describes, and the two disagree about how large the file is. This is the "
                "situation where a file browser shows one size, the file system reports another, "
                "and every read fails.\n\n"
                "The bytes the compression overwrote are gone. No tool can bring them back, and "
                "this tool will not pretend otherwise. What is still intact is worth copying out "
                "before anything else touches the disk."
            ),
            true,
            true
        );
    }
    // The second is a database that is simply compressed or sparse. It is not
    // damaged, but SQLite cannot write to it, and it is the reason a user who
    // "optimised" their drive ended up here.
    if (sqliteResultCode==SQLITE_READONLY || sqliteResultCode==SQLITE_CANTOPEN) {
        if (looksCompressedOrSparse(fileMetadata)) {
            return buildMatch(
                string(PATTERN_COMPRESSED_WHILE_OPEN),
                SeverityLevel::URGENT,
                string(
                    "This database is being stored compressed or sparsely, which occupies far less "
                    "space than its contents need. SQLite cannot write to a file like this, and the "
                    "agent will fail the next time it tries to save a message.\n\n"
                    "Compressing a SQLite database does not shrink it. The pages have no repeated "
                    "patterns for a compressor to find, so the algorithm writes new clusters beside "
                    "the old ones and the file only gets bigger. Marking the file as sparse is "
                    "harmless, but compressing it is not."
                ),
                true,
                false
            );
        }
    }
    // The third is the plain unreadable case: the operating system would not read
    // the file and there is no further explanation available.
    if (sqliteResultCode==SQLITE_IOERR_READ) {
        return buildMatch(
            string(PATTERN_UNREADABLE_FILE),
            SeverityLevel::BEYOND_RECOVERY,
            string(
                "The operating system cannot read this file. That usually means the file was "
                "modified, truncated, or compressed while the database was open, or that the drive "
                "itself is failing.\n\n"
                "Run the drive's own check first. If the drive reports errors, replace it before "
                "doing anything else, because every further read makes the situation worse."
            ),
            false,
            true
        );
    }
    // The fourth is a file that is not a database at all.
    if (sqliteResultCode==SQLITE_NOTADB || sqliteResultCode==SQLITE_PROTOCOL) {
        return buildMatch(
            string(PATTERN_NOT_A_DATABASE),
            SeverityLevel::URGENT,
            string(
                "This file is not a SQLite database. Either the path points at a different kind of "
                "file, or the beginning of the file has been overwritten.\n\n"
                "If the path is wrong, point the tool at the right file. If the path is right, "
                "something has overwritten the start of the file and the sessions inside it are "
                "probably gone."
            ),
            false,
            true
        );
    }
    // The fifth is a write that was interrupted, which SQLite can usually replay.
    if (lockStatus.hasRollbackJournal || lockStatus.hasWriteAheadLog) {
        return buildMatch(
            string(PATTERN_INTERRUPTED_WRITE),
            SeverityLevel::ATTENTION,
            string(
                "A write to this database was interrupted, and SQLite has a journal waiting to be "
                "replayed. This is usually harmless: the next open replays the journal and the "
                "database becomes consistent again. It is worth doing before anything else touches "
                "the file."
            ),
            true,
            false
        );
    }
    // A database that another program holds open is not damaged, but nothing can
    // safely be done to it until that program is closed. The user is told which
    // programs to close, which is more useful than a lock error code.
    if (lockStatus.isLocked) {
        return buildMatch(
            string(PATTERN_IN_USE),
            SeverityLevel::ATTENTION,
            string(
                "Another program has this database open. SQLite allows that, which means the "
                "coding agent can still be writing while a tool reads. Nothing is wrong, but "
                "nothing should be changed either.\n\n"
            ) + lockStatus.explanation,
            true,
            false
        );
    }
    // Structural damage that the operating system can still read. SQLite can
    // describe it, and the tool can often rebuild what is missing.
    if (sqliteResultCode==SQLITE_CORRUPT || sqliteResultCode==SQLITE_CORRUPT_VTAB) {
        return buildMatch(
            string(PATTERN_STRUCTURALLY_DAMAGED),
            SeverityLevel::URGENT,
            string(
                "SQLite can read this file but finds the structure inside it damaged. The sessions "
                "themselves are usually still there, because the damage is normally in an index or "
                "in the free page list rather than in the conversation data.\n\n"
                "Copy the file somewhere safe before doing anything else, then let SQLite rebuild "
                "what it can."
            ),
            true,
            false
        );
    }
    // A drive with very little room is worth naming, because it is the cause behind
    // a surprising number of these problems. The threshold is the same one the
    // compaction check uses, so the warning and the refusal agree.
    if (availableSpace.isValid
        && availableSpace.availableBytes < SpaceRequirementThresholdForWarning) {
        return buildMatch(
            string(PATTERN_DISK_FULL),
            SeverityLevel::URGENT,
            string(
                "There is very little free space on the drive holding this database: "
            ) + to_string(availableSpace.availableBytes)
            + " bytes. That is the most common reason a coding agent database is damaged, because "
            + "SQLite cannot finish a write it has already started. Free some space before doing "
            + "anything else, and check the drive is healthy.",
            true,
            false
        );
    }
    // The file occupies slightly more space than its contents need, which is
    // normal and worth saying so the user does not worry about it.
    if (fileMetadata.logicalSizeBytes>0
        && fileMetadata.allocatedSizeBytes>fileMetadata.logicalSizeBytes) {
        return buildMatch(
            string(PATTERN_COMPRESSED_OR_SPARSE),
            SeverityLevel::INFORMATION,
            string(
                "This file occupies slightly more space than its contents need, which is normal. "
                "A file system allocates space in whole clusters, and the last cluster of a file is "
                "usually only partly used."
            ),
            true,
            false
        );
    }
    return noMatch();
}
vector<string> KnownFailurePatterns::allPatternNames() {
    // The list is fixed rather than built while matching, so that the diagnose
    // command and the tests always see the same set.
    return vector<string>{
        string(PATTERN_COMPRESSED_WHILE_OPEN),
        string(PATTERN_UNREADABLE_FILE),
        string(PATTERN_INTERRUPTED_COMPRESSION),
        string(PATTERN_NOT_A_DATABASE),
        string(PATTERN_INTERRUPTED_WRITE),
        string(PATTERN_IN_USE),
        string(PATTERN_DISK_FULL),
        string(PATTERN_COMPRESSED_OR_SPARSE),
        string(PATTERN_STRUCTURALLY_DAMAGED)
    };
}
}
