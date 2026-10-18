// The macOS implementation of the PlatformMetadata class.
//
// Every function here is defined once per platform so that the calling code
// never contains an operating system conditional. macOS has no `statx` call, so
// the answers come from `stat`, which reports the same fields in one call.
#include <cerrno>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
// Provides `struct statfs` on the Berkeley derived systems, macOS among them.
#include <sys/mount.h>
#else
// glibc declares the same structure under a different name.
#include <sys/vfs.h>
#endif
#if defined(__APPLE__)
// Provides `clonefile`, which makes one file share another's blocks on APFS.
#include <sys/clonefile.h>
#endif
// Provides `kill`, which asks whether a process is still running.
#include <signal.h>
// Provides the size the C library reserves for a host name: `HOST_NAME_MAX` on
// Linux, and `_POSIX_HOST_NAME_MAX`, the POSIX guarantee, on macOS.
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
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
    // `struct stat` is the classic file information structure. `lstat` is used
    // rather than `stat` so that a symbolic link is described as itself instead
    // of as whatever it points at. Principle I/O-19 says never follow a symbolic
    // link unless the user asked for it.
    struct stat fileStatus;
    if (lstat(filePath.c_str(), &fileStatus)!=0) {
        const int failureCode=errno;
        result.errorCode=failureCode;
        result.exists=failureCode!=ENOENT && failureCode!=ENOTDIR;
        return result;
    }
    result.exists=true;
    // `S_ISDIR` is a macro that tests the file type bits inside the mode field.
    result.isDirectory=S_ISDIR(fileStatus.st_mode);
    // `st_size` is the logical size in bytes, which is the number a file browser
    // shows.
    result.logicalSizeBytes=static_cast<long long>(fileStatus.st_size);
    // `st_blocks` counts five hundred and twelve byte blocks, the unit the kernel
    // uses for every file system. Multiplying by five hundred and twelve converts
    // to bytes. On an APFS volume with compression this is often smaller than the
    // logical size, which is exactly the situation the report needs to show.
    result.allocatedSizeBytes=static_cast<long long>(fileStatus.st_blocks) * 512LL;
    // The modification time is a pair of values: seconds since the Unix epoch, and a
    // nanosecond part. Combining them into milliseconds keeps ordering information
    // that whole seconds would lose.
    //
    // The field is spelled `st_mtimespec` on the BSD family, which includes macOS,
    // and `st_mtim` under glibc. Both hold the same `struct timespec`, and the
    // difference is only the name the header chose.
    const struct timespec& modifiedTime=
#if defined(__APPLE__)
        fileStatus.st_mtimespec;
#else
        fileStatus.st_mtim;
#endif
    result.modificationEpochMilliseconds=
        static_cast<long long>(modifiedTime.tv_sec) * 1000LL
        + static_cast<long long>(modifiedTime.tv_nsec) / 1000000LL;
    return result;
}
DiskSpace PlatformMetadata::readDiskSpace(const string& pathOnVolume) {
    MetadataCallCounter::record();
    DiskSpace result;
    // The question is about the file system that holds the path. When the caller
    // passed a file rather than a folder, its parent folder is used, so the answer
    // is the same whichever kind of path was given.
    string volumePath=pathOnVolume;
    const FileMetadata pathStatus=readFileMetadata(pathOnVolume);
    if (pathStatus.exists && !pathStatus.isDirectory) {
        const size_t lastSeparator=pathOnVolume.find_last_of('/');
        if (lastSeparator!=string::npos) {
            volumePath=pathOnVolume.substr(0, lastSeparator);
        }
    }
    // `struct statfs` describes a mounted file system. `f_bsize` is the fundamental
    // file system block size, which is the unit the block counts below use.
    struct statfs fileSystemStatus;
    if (statfs(volumePath.c_str(), &fileSystemStatus)!=0) {
        result.errorCode=errno;
        return result;
    }
    result.isValid=true;
    // `f_bavail` is the number of blocks an unprivileged program may still use,
    // which is the number that matters when deciding whether an operation fits.
    result.availableBytes=
        static_cast<long long>(fileSystemStatus.f_bavail) * static_cast<long long>(fileSystemStatus.f_bsize);
    result.totalBytes=
        static_cast<long long>(fileSystemStatus.f_blocks) * static_cast<long long>(fileSystemStatus.f_bsize);
    result.freeBytesTotal=
        static_cast<long long>(fileSystemStatus.f_bfree) * static_cast<long long>(fileSystemStatus.f_bsize);
    return result;
}
bool PlatformMetadata::tryCreateCopyOnWriteClone(const string& sourcePath, const string& destinationPath) {
#if defined(__APPLE__)
    // `clonefile` is the Apple request that makes the destination share the
    // source's blocks. APFS supports it; a volume that does not, or a destination
    // that already exists, answers with an error, and the caller reads that as
    // "copy the bytes instead".
    return clonefile(sourcePath.c_str(), destinationPath.c_str(), 0)==0;
#else
    // This file is also compiled on Linux by the compile check, where the clone
    // facility does not exist and there is nothing to imitate.
    (void)sourcePath;
    (void)destinationPath;
    return false;
#endif
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
    // `kill` with signal zero performs the permission and existence checks without
    // delivering anything, which is the traditional way to ask whether a process
    // is still alive. The negative form `kill(-pid, 0)` asks about a process
    // group, which would give a false positive for a shell, so the positive form
    // is used.
    return kill(static_cast<pid_t>(processIdentifier), 0)==0 || errno==EPERM;
}
string PlatformMetadata::hostName() {
    // `HOST_NAME_MAX` is a Linux extension: with glibc it is defined, but macOS
    // defines only the POSIX guarantee `_POSIX_HOST_NAME_MAX`, and using the
    // Linux name there is a compile error. The POSIX value is the smaller of the
    // two, so a buffer of that size plus one is correct on every system this
    // tool supports. The buffer is a fixed size array, so the read cannot
    // allocate.
#if defined(HOST_NAME_MAX)
    constexpr size_t hostNameBufferSize=HOST_NAME_MAX + 1;
#else
    constexpr size_t hostNameBufferSize=_POSIX_HOST_NAME_MAX + 1;
#endif
    char hostBuffer[hostNameBufferSize];
    if (gethostname(hostBuffer, sizeof(hostBuffer))!=0) {
        return string("unknown-host");
    }
    // `gethostname` does not null terminate when the name fills the buffer, so
    // the length is clamped before constructing the string.
    const size_t hostLength=strnlen(hostBuffer, sizeof(hostBuffer));
    return string(hostBuffer, hostLength);
}
namespace {
// Reads the home directory of the current user out of the passwd database.
//
// The passwd file is read line by line until the current user name matches. The
// file is small and lives in the page cache, so this costs one open and a few
// hundred bytes. It is only reached when `HOME` is unset, which on this platform
// means a launch daemon start rather than a user command. It is a file local
// function because no other platform reads the passwd file this way.
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

string PlatformMetadata::homeDirectory() {
    // `HOME` is the only variable a normal macOS session sets. The passwd
    // database is the fallback for a process started without an environment, such
    // as a launch daemon.
    string homeFromEnvironment;
    if (readEnvironmentVariable("HOME", homeFromEnvironment) && !homeFromEnvironment.empty()) {
        return homeFromEnvironment;
    }
    return readPasswdHomeDirectory();
}

string PlatformMetadata::programDataDirectory() {
    // On macOS the conventional location for a system wide configuration file is
    // `/Library/Application Support`, which needs no environment variable to
    // find. The path contains a space, which is legal in a macOS path.
    return string("/Library/Application Support");
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
