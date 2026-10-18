// The PlatformMetadata class, the single place the tool asks the operating
// system about files, disks, processes, and time.
//
// Principle I/O-1 of the specification says file metrics come from metadata calls
// and never from reading the file. This class is where that rule is carried out.
// It also keeps a counter of the metadata calls it has made, which is how the
// benchmark suite proves the diagnostic pass stays inside its budget of fewer
// than twenty system calls.
#ifndef AGENT_DB_DOCTOR_FILESYSTEM_PLATFORMMETADATA_HPP
#define AGENT_DB_DOCTOR_FILESYSTEM_PLATFORMMETADATA_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::FileSystem {
// The FileMetadata value type describes one path, as the operating system sees
// it.
//
// This is a plain value type returned by value. Modern C++ constructs it
// directly at the call site, so there is no copy and no allocation. The fields
// are ordered from widest to narrowest so that the compiler adds no padding,
// which is what keeps the structure under sixty four bytes.
struct FileMetadata final {
    // True when the path exists and can be examined at all.
    bool exists=false;
    // True when the path is a directory rather than a file.
    bool isDirectory=false;
    // The number of bytes the file would occupy if every byte were stored. This
    // is the number the user recognises from a file browser.
    long long logicalSizeBytes=0;
    // The number of bytes the file actually occupies on the volume, which is
    // larger than the logical size on a file system with a cluster size, and
    // smaller when the volume supports compression or sparse files.
    long long allocatedSizeBytes=0;
    // When the file was last written, as milliseconds since the Unix epoch. The
    // epoch is 1970-01-01, and the unit is milliseconds rather than seconds so
    // that a busy database does not lose ordering information.
    long long modificationEpochMilliseconds=0;
    // The platform error number when `exists` is false and the path was not
    // simply absent, and zero when the call succeeded. A non zero value here
    // means the path exists but could not be examined, which is a different
    // problem from the file not being there.
    int errorCode=0;
};
// The DiskSpace value type describes the volume that holds a given path.
struct DiskSpace final {
    // The total size of the volume in bytes.
    long long totalBytes=0;
    // The number of bytes a normal program may still write.
    long long availableBytes=0;
    // The number of bytes that are free in total. This is usually larger than
    // `availableBytes`, because some of the free space is reserved for the
    // operating system's own bookkeeping.
    long long freeBytesTotal=0;
    // True when the operating system answered the query.
    bool isValid=false;
    // The platform error number when `isValid` is false.
    int errorCode=0;
};
// The PlatformMetadata class wraps the handful of operating system calls the tool
// needs, one per platform.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create: a Java
// developer would recognise this as a class of static factory methods. The three
// platform implementations live in PlatformMetadataWindows.cpp,
// PlatformMetadataLinux.cpp, and PlatformMetadataMacOS.cpp, and exactly one of
// them is compiled into the binary.
class PlatformMetadata final {
public:
    PlatformMetadata()=delete;
    // Reads the metadata for one path.
    //
    // This performs exactly one metadata system call and never opens the file.
    // That is the whole point of the class: the size of a file must never be
    // discovered by reading it.
    static FileMetadata readFileMetadata(const string& filePath);
    // Reads the space available on the volume that holds `pathOnVolume`. The path
    // may be a file or a directory, or may not exist at all, as long as its
    // parent volume can be identified.
    static DiskSpace readDiskSpace(const string& pathOnVolume);
    // Creates `destinationPath` as a copy on write clone of `sourcePath` when the
    // file system can. A clone shares its blocks with the original until either is
    // written, so on APFS, on btrfs and XFS, and on ReFS or a Windows Dev Drive a
    // safety copy costs no space and almost no time.
    //
    // Returns false on every file system that cannot, and the caller copies the
    // bytes instead. A false answer is never an error: it only means this volume
    // has no cloning to offer. A destination that already exists is replaced on
    // success and removed again on failure, so the caller never has to clean up
    // after a refusal.
    static bool tryCreateCopyOnWriteClone(const string& sourcePath, const string& destinationPath);
    // The number of metadata system calls this process has made. The benchmark
    // suite reads it to assert that a diagnostic pass stays inside the budget.
    static long long metadataCallCount();
    // Resets the counter to zero. Called at the start of a measured section.
    static void resetMetadataCallCount();
    // A number of nanoseconds from a monotonic clock, meaning a clock that cannot
    // be adjusted backwards by a time change. The tool uses it for every duration
    // it reports, because a wall clock that jumps would produce a negative
    // measurement.
    static long long monotonicNanoseconds();
    // Pauses for the given number of milliseconds.
    static void sleepMilliseconds(int milliseconds);
    // The current process identifier, as reported by the operating system.
    static int currentProcessIdentifier();
    // True when a process with the given identifier exists. Used to tell whether
    // a coding agent is running before the tool touches its database.
    static bool isProcessRunning(int processIdentifier);
    // The name of the computer, used only in log lines.
    static string hostName();
    // The current user's home directory, for example `/home/ann` or
    // `C:\Users\ann`. Returns the empty string when the environment does not say.
    static string homeDirectory();
    // The directory where programs keep shared configuration, for example
    // `C:\ProgramData` or `/etc`. Used as the parent of the service
    // configuration file on Windows.
    static string programDataDirectory();
    // True when standard output is attached to a terminal. When it is false the
    // presentation layer must not emit any escape sequences.
    static bool isStandardOutputATerminal();
    // True when the named environment variable is set to a non empty value.
    // Returns the value through `valueOut` when it is, and false otherwise.
    static bool readEnvironmentVariable(const string& variableName, string& valueOut);
private:
};
}
#endif
