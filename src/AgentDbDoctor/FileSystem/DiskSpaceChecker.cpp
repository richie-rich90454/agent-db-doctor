// The implementation of the DiskSpaceChecker class.
//
// The arithmetic here is deliberately pessimistic. The specification asks the
// compact command to refuse to run rather than fill a drive, and the only way to
// honour that is to assume the worst case: that every single page of the
// database is still live and the compacted copy will be exactly as large as the
// original.
#include <string>
// Provides `max`, the standard library function that returns the larger of two
// values of the same type.
#include <algorithm>
// Provides the PlatformMetadata class and the DiskSpace value type.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the DiskSpaceChecker class declaration.
#include "AgentDbDoctor/FileSystem/DiskSpaceChecker.hpp"
using namespace std;
using namespace AgentDbDoctor;
namespace AgentDbDoctor::FileSystem {
// The extra room added to every requirement. A file system reserves a percentage
// of a volume for its own bookkeeping, and a copy that lands exactly on the last
// free byte would fail. Five percent is a comfortable margin that still refuses
// to run on a genuinely full drive.
static constexpr double SAFETY_MARGIN_FRACTION=0.05;
// The smallest margin that is ever added, so that a tiny database still gets a
// little room for the file system metadata.
static constexpr long long MINIMUM_SAFETY_MARGIN_BYTES=16LL * 1024LL * 1024LL;
// The fraction of the file a memory mapped integrity check needs beyond what the
// page cache already holds. A quick check reads pages in order and the kernel
// keeps a window of them, so the real figure is small.
static constexpr double INTEGRITY_CHECK_FRACTION=0.02;
SpaceRequirement DiskSpaceChecker::requiredBytesForBackup(long long currentFileSizeBytes) {
    SpaceRequirement requirement;
    const long long margin=max(
        static_cast<long long>(static_cast<double>(currentFileSizeBytes) * SAFETY_MARGIN_FRACTION),
        MINIMUM_SAFETY_MARGIN_BYTES
    );
    requirement.requiredBytes=currentFileSizeBytes + margin;
    requirement.requirementKind="backup";
    requirement.explanation="A safety copy of the database needs about "
        + to_string(requirement.requiredBytes) + " bytes of free space, which is the size of the file plus a small margin for the file system.";
    return requirement;
}
SpaceRequirement DiskSpaceChecker::requiredBytesForCompaction(long long currentFileSizeBytes) {
    SpaceRequirement requirement;
    const long long margin=max(
        static_cast<long long>(static_cast<double>(currentFileSizeBytes) * SAFETY_MARGIN_FRACTION),
        MINIMUM_SAFETY_MARGIN_BYTES
    );
    // The worst case for `VACUUM INTO` is that every page is still live, so the
    // new file is exactly as large as the old one. Requiring the full size plus a
    // margin is the honest answer.
    requirement.requiredBytes=currentFileSizeBytes + margin;
    requirement.requirementKind="compaction";
    requirement.explanation="Compaction writes a brand new database before replacing the old one, so it needs free space equal to the whole file, about "
        + to_string(requirement.requiredBytes) + " bytes, plus a small margin for the file system.";
    return requirement;
}
SpaceRequirement DiskSpaceChecker::requiredBytesForIntegrityCheck(long long currentFileSizeBytes) {
    SpaceRequirement requirement;
    const long long estimatedBytes=static_cast<long long>(
        static_cast<double>(currentFileSizeBytes) * INTEGRITY_CHECK_FRACTION
    );
    requirement.requiredBytes=max(estimatedBytes, MINIMUM_SAFETY_MARGIN_BYTES);
    requirement.requirementKind="integrity-check";
    requirement.explanation="An integrity check reads the file without changing it. It needs about "
        + to_string(requirement.requiredBytes) + " bytes of spare room for the operating system page cache while it works.";
    return requirement;
}
bool DiskSpaceChecker::hasEnoughSpace(const DiskSpace& space, const SpaceRequirement& requirement) {
    if (!space.isValid) {
        // When the operating system would not answer, the safe answer is no. The
        // user can always pass a flag that skips the check, but the default has to
        // be the one that cannot fill a drive.
        return false;
    }
    return space.availableBytes >= requirement.requiredBytes;
}
string DiskSpaceChecker::describeAvailableSpace(const DiskSpace& space) {
    // The numbers are returned as bytes rather than as formatted text, because
    // this layer must not depend on the presentation layer. The report renderer
    // turns them into something a person can read.
    if (!space.isValid) {
        return string("The free space on the drive could not be read.");
    }
    return to_string(space.availableBytes) + " bytes free of " + to_string(space.totalBytes) + " bytes.";
}
}
