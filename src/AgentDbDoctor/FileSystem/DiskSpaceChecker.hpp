// The DiskSpaceChecker class, which answers one question: is there enough room on
// the volume to do what the user asked for?
//
// The specification requires the compact command to refuse to run when there is
// not enough free space, because `VACUUM INTO` writes a second full copy of the
// database before replacing the first. Guessing at that requirement is how a tool
// fills a drive that was already nearly full, so the check is arithmetic on
// measured numbers rather than a guess.
#ifndef AGENT_DB_DOCTOR_FILESYSTEM_DISKSPACECHECKER_HPP
#define AGENT_DB_DOCTOR_FILESYSTEM_DISKSPACECHECKER_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the DiskSpace value type and the PlatformMetadata class.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::FileSystem {
// The SpaceRequirement value type holds a computed number of bytes and the
// sentence explaining where it came from.
struct SpaceRequirement final {
    // The number of bytes the operation needs.
    long long requiredBytes=0;
    // A sentence in plain English, so the report never shows a bare number.
    string explanation;
    // How the requirement was worked out, which is one of `copy`, `compaction`,
    // `backup`, or `unknown`. It is a string rather than an enum because it is
    // only ever printed, and a string cannot be mistyped into a switch.
    string requirementKind;
};
// The DiskSpaceChecker class computes space requirements and compares them with
// what the volume actually has.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class DiskSpaceChecker final {
public:
    DiskSpaceChecker()=delete;
    // The free space needed to make a safety copy of a database before changing
    // it. A copy needs room equal to the file it copies, plus a small margin so
    // the copy is not rejected by a file system that reserves a few percent for
    // its own bookkeeping.
    //
    // `currentFileSizeBytes` is the logical size of the file, and the returned
    // requirement is a little larger than that so the copy is not rejected by a
    // file system that reserves a few percent for its own bookkeeping.
    static SpaceRequirement requiredBytesForBackup(long long currentFileSizeBytes);
    // The free space needed to compact a database.
    //
    // Compaction is done with `VACUUM INTO`, which writes a brand new database
    // containing only the live pages. At its worst it needs room for the whole
    // file, because every page could still be live. The requirement returned
    // here is that worst case plus the margin, because promising less would
    // risk filling a drive the user was already struggling with.
    static SpaceRequirement requiredBytesForCompaction(long long currentFileSizeBytes);
    // The free space needed to run `PRAGMA quick_check` on a database, which
    // reads pages through the memory map and writes nothing permanent. The
    // requirement is small, but a memory mapped read still needs room for the
    // page cache, so it is a fraction of the file rather than zero.
    static SpaceRequirement requiredBytesForIntegrityCheck(long long currentFileSizeBytes);
    // True when the volume has at least `requiredBytes` free. `space` is the
    // value previously read by `PlatformMetadata::readDiskSpace`, so this
    // method performs no system call of its own.
    static bool hasEnoughSpace(const DiskSpace& space, const SpaceRequirement& requirement);
    // A sentence describing the free space, for example "12.4 GB free of 250 GB".
    static string describeAvailableSpace(const DiskSpace& space);
private:
};
}
#endif
