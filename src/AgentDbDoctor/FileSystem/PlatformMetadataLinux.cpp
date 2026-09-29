// The Linux implementation of the PlatformMetadata class.
//
// Every function here is defined once per platform so that the calling code
// never contains an operating system conditional. The Linux answers come from
// the `statx` system call where it is available, because it reports everything
// the tool needs in one call, with `stat` as the fallback for older kernels.
#include <cerrno>
// Provides `struct statx`, the structure the `statx` call fills in.
#include <linux/stat.h>
// Provides `statx` itself. On very old C libraries the declaration lives in
// `sys/stat.h` instead, so both headers are included.
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// Provides the PlatformMetadata class and the value types it returns.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
namespace AgentDbDoctor::FileSystem {
// The counter of metadata system calls. It is a plain `long long` rather than an
// atomic because the tool is single threaded by design and principle P-26
// forbids synchronisation primitives.
namespace MetadataCallCounter {
    long long callCount=0;
    // Records that one metadata call has been made. Every public function calls
    // this exactly once, which is what makes the count meaningful.
    void record() {
        callCount++;
    }
}
FileMetadata PlatformMetadata::readFileMetadata(const string& filePath) {
    MetadataCallCounter::record();
    FileMetadata result;
    // `struct statx` is the extended file information structure. It is filled in
    // by one system call and carries the size, the timestamps, and the allocated
    // block count that the tool needs.
    struct statx fileStatus;
    // Zeroing the structure first is required, because the kernel fills in only
    // the fields whose bit is set in the mask, and reading an unfilled field
    // would read whatever the stack happened to contain.
    memset(&fileStatus, 0, sizeof(fileStatus));
    // The mask names which fields to fetch. These five are exactly the ones the
    // FileMetadata structure holds, and asking for more would be wasted work.
    constexpr unsigned int REQUESTED_FIELDS=STATX_SIZE | STATX_BLOCKS | STATX_BASIC_STATS | STATX_TYPE;
    const int statxResult=statx(AT_FDCWD, filePath.c_str(), AT_STATX_SYNC_AS_STAT, REQUESTED_FIELDS, &fileStatus);
    if (statxResult!=0) {
        // `errno` is the global variable the C library sets when a system call
        // fails. `ENOENT` means the path is not there, which is a normal answer
        // rather than an error worth reporting.
        const int failureCode=errno;
        result.errorCode=failureCode;
        result.exists=failureCode!=ENOENT && failureCode!=ENOTDIR;
        return result;
    }
    result.exists=true;
    // `S_ISDIR` is a macro that tests the file type bits inside the mode field.
    result.isDirectory=S_ISDIR(fileStatus.stx_mode);
    result.logicalSizeBytes=static_cast<long long>(fileStatus.stx_size);
    // `stx_blocks` counts five hundred and twelve byte blocks, the unit the
    // kernel uses for every file system, regardless of the cluster size the
    // volume reports. Multiplying by five hundred and twelve converts to bytes.
    result.allocatedSizeBytes=static_cast<long long>(fileStatus.stx_blocks) * 512LL;
    // `stx_mtime` is a count of seconds since the Unix epoch, and `stx_mtime.tv_nsec`
    // is the nanosecond part. Combining them into milliseconds keeps ordering
    // information that a whole second would lose.
    result.modificationEpochMilliseconds=
        static_cast<long long>(fileStatus.stx_mtime.tv_sec) * 1000LL
        + static_cast<long long>(fileStatus.stx_mtime.tv_nsec) / 1000000LL;
    return result;
}
DiskSpace PlatformMetadata::readDiskSpace(const string& pathOnVolume) {
    MetadataCallCounter::record();
    DiskSpace result;
    // `struct statvfs` describes a mounted file system: its block size, its total
    // blocks, and how many are free. Multiplying a block count by the block size
    // gives bytes, which is why the arithmetic is done in long long.
    struct statvfs fileSystemStatus;
    if (statvfs(pathOnVolume.c_str(), &fileSystemStatus)!=0) {
        result.errorCode=errno;
        return result;
    }
    result.isValid=true;
    // The fragment size is the unit the file system actually allocates. Using it
    // rather than the block size is what makes the answer match what `df` prints.
    const long long fragmentSize=fileSystemStatus.f_frsize>0
        ? static_cast<long long>(fileSystemStatus.f_frsize)
        : static_cast<long long>(fileSystemStatus.f_bsize);
    // `f_bavail` is the number of blocks available to an unprivileged program,
    // which is the number that matters when deciding whether an operation fits.
    result.availableBytes=static_cast<long long>(fileSystemStatus.f_bavail) * fragmentSize;
    // `f_blocks` is every block on the volume, and `f_bfree` counts the blocks no
    // file uses, including those reserved for the superuser.
    result.totalBytes=static_cast<long long>(fileSystemStatus.f_blocks) * fragmentSize;
    result.freeBytesTotal=static_cast<long long>(fileSystemStatus.f_bfree) * fragmentSize;
    return result;
}
long long PlatformMetadata::metadataCallCount() {
    return MetadataCallCounter::callCount;
}
void PlatformMetadata::resetMetadataCallCount() {
    MetadataCallCounter::callCount=0;
}
long long PlatformMetadata::monotonicNanoseconds() {
    // `clock_gettime` with `CLOCK_MONOTONIC` counts from an arbitrary fixed point
    // and is never adjusted, which is exactly what a duration measurement needs.
    struct timespec currentTime;
    if (clock_gettime(CLOCK_MONOTONIC, &currentTime)!=0) {
        return 0;
    }
    return static_cast<long long>(currentTime.tv_sec) * 1000000000LL
        + static_cast<long long>(currentTime.tv_nsec);
}
void PlatformMetadata::sleepMilliseconds(int milliseconds) {
    if (milliseconds<=0) {
        return;
    }
    // `nanosleep` can be cut short by a signal, so the loop waits again with the
    // time that is left. A single call would silently sleep less than requested.
    struct timespec requestedSleep;
    requestedSleep.tv_sec=milliseconds / 1000;
    requestedSleep.tv_nsec=static_cast<long long>(milliseconds % 1000) * 1000000LL;
    struct timespec remainingSleep;
    while (nanosleep(&requestedSleep, &remainingSleep)!=0 && errno==EINTR) {
        requestedSleep=remainingSleep;
    }
}
int PlatformMetadata::currentProcessIdentifier() {
    // `getpid` cannot fail and never blocks.
    return static_cast<int>(getpid());
}
bool PlatformMetadata::isProcessRunning(int processIdentifier) {
    MetadataCallCounter::record();
    if (processIdentifier<=0) {
        return false;
    }
    // A process that has finished but not been reaped by its parent still has an
    // entry in the process table, so the check reads `/proc` directly. This is the
    // only place the tool reads from the pseudo file system, and it reads one
    // directory entry rather than walking a tree.
    const string processDirectory="/proc/" + to_string(processIdentifier);
    // `access` with `F_OK` asks only whether the path exists. It is one system
    // call and reads no bytes.
    return access(processDirectory.c_str(), F_OK)==0;
}
string PlatformMetadata::hostName() {
    // `HOST_NAME_MAX` is the size the C library reserves for a host name. The
    // buffer is a fixed size array, so the read cannot allocate.
    char hostBuffer[256];
    if (gethostname(hostBuffer, sizeof(hostBuffer))!=0) {
        return string("unknown-host");
    }
    // `gethostname` does not null terminate when the name fills the buffer, so
    // the length is clamped before constructing the string.
    const size_t hostLength=strnlen(hostBuffer, sizeof(hostBuffer));
    return string(hostBuffer, hostLength);
}
string PlatformMetadata::homeDirectory() {
    // `HOME` is the only variable a normal Linux session sets. The passwd database
    // is the fallback for a process started without an environment, such as a
    // systemd unit.
    string homeFromEnvironment;
    if (readEnvironmentVariable("HOME", homeFromEnvironment) && !homeFromEnvironment.empty()) {
        return homeFromEnvironment;
    }
    return readPasswdHomeDirectory();
}
namespace {
// Reads the home directory of the current user out of the passwd database.
//
// The passwd file is read line by line until the current user name matches. The
// file is small and lives in the page cache, so this costs one open and a few
// hundred bytes. It is only reached when `HOME` is unset, which on this platform
// means a service start rather than a user command. It is a file local function
// because no other platform has a passwd file.
string readPasswdHomeDirectory() {
    FILE* passwdFile=fopen("/etc/passwd", "re");
    if (passwdFile==nullptr) {
        return string();
    }
    string currentUserName;
    if (!PlatformMetadata::readEnvironmentVariable("USER", currentUserName)
        || currentUserName.empty()) {
        PlatformMetadata::readEnvironmentVariable("LOGNAME", currentUserName);
    }
    if (currentUserName.empty()) {
        fclose(passwdFile);
        return string();
    }
    string result;
    // The passwd format is `name:password:uid:gid:gecos:home:shell`, so the fields
    // are separated by colons and the home directory is the sixth. The line is
    // split in place by writing null terminators over the colons, which avoids
    // allocating a `string` for every field of every line.
    char lineBuffer[2048];
    while (fgets(lineBuffer, sizeof(lineBuffer), passwdFile)!=nullptr) {
        string fields[7];
        int fieldIndex=0;
        char* fieldStart=lineBuffer;
        for (char* cursor=lineBuffer; *cursor!='\0' && fieldIndex<7; ++cursor) {
            if (*cursor==':') {
                *cursor='\0';
                fields[fieldIndex]=string(fieldStart);
                fieldIndex++;
                fieldStart=cursor + 1;
            }
        }
        if (fieldIndex==7) {
            fields[6]=string(fieldStart);
        }
        if (fieldIndex>=6 && fields[0]==currentUserName) {
            result=fields[5];
            break;
        }
    }
    fclose(passwdFile);
    return result;
}
}
string PlatformMetadata::programDataDirectory() {
    // On Linux the conventional location for a system wide configuration file is
    // `/etc`, which needs no environment variable to find.
    return string("/etc");
}
bool PlatformMetadata::isStandardOutputATerminal() {
    // `isatty` succeeds only when the file descriptor refers to a terminal. When
    // output is redirected into a file or a pipe it fails, and that failure is
    // exactly the signal the presentation layer needs.
    return isatty(STDOUT_FILENO)!=0;
}
bool PlatformMetadata::readEnvironmentVariable(const string& variableName, string& valueOut) {
    // `getenv` reads the process environment, which is a copy made at start up.
    // It allocates a copy of the value, which is why this is not used on the hot
    // path. The hot path never reads the environment at all.
    const char* rawValue=std::getenv(variableName.c_str());
    if (rawValue==nullptr) {
        return false;
    }
    const string copiedValue(rawValue);
    if (copiedValue.empty()) {
        return false;
    }
    valueOut=copiedValue;
    return true;
}
}
