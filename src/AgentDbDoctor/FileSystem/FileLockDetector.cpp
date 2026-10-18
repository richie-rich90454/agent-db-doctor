// The implementation of the FileLockDetector class.
//
// The approach is platform specific, and the reason is that Windows and the Unix
// family answer the question in completely different ways. Windows share modes
// are enforced by the kernel, so asking for exclusive access is enough. Unix file
// locks are advisory byte range locks stored in the kernel, so the only reliable
// question to ask is whether a conflicting lock can be taken.
//
// Whichever platform is compiled, exactly one branch is active. That is why the
// conditionals live inside this file rather than in three separate files: the
// answer is short enough that one file with three branches is easier to keep
// correct than three files that must agree.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `memset`, which zeroes a structure before the kernel fills part of it.
#include <cstring>
// Provides the FileLockStatus value type.
#include "AgentDbDoctor/FileSystem/FileLockDetector.hpp"
// Provides PlatformMetadata, used to look for the side files.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the OperatingSystemErrorTranslator, which turns errno into a sentence.
#include "AgentDbDoctor/Common/Exceptions.hpp"
#if defined(_WIN32)
// Declares the Win32 file functions used below.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
// Provides `open`, `close`, and the file open flags.
#include <fcntl.h>
// Provides `fcntl` and the `struct flock` byte range lock type.
#include <unistd.h>
#include <sys/types.h>
// Provides the `errno` variable and its named values.
#include <cerrno>
#endif
using namespace std;
using namespace AgentDbDoctor;
namespace AgentDbDoctor::FileSystem {
// The name suffixes SQLite gives the two side files that sit next to a database.
// They are constants because they must match SQLite's own naming exactly.
static constexpr const char* WRITE_AHEAD_LOG_SUFFIX="-wal";
// The shared memory file a write ahead log database keeps its locks in. This is
// the file a live connection actually holds, which is why it is probed.
static constexpr const char* SHARED_MEMORY_SUFFIX="-shm";
// The rollback journal, which exists only when a write was interrupted.
static constexpr const char* ROLLBACK_JOURNAL_SUFFIX="-journal";
// What one probe of one path answered.
enum class PathAccessAnswer {
    // The path can be locked exclusively, so nobody else is holding it.
    Free,
    // The path does not exist. Nothing can be holding a file that is not there.
    Missing,
    // Another program holds the path.
    InUse,
    // The question could not be answered, for example because of a permission
    // problem. The caller treats this like a lock for the database itself and like
    // a free file for a side file, which is the safe reading on both counts.
    CannotTell
};
#if defined(_WIN32)
// Asks Windows whether `filePath` can be opened with exclusive access. The share
// mode of zero is what makes any other open handle, whether for reading or for
// writing, refuse the request.
static PathAccessAnswer probePathAccess(const string& filePath, int& errorCodeOut) {
    const int wideCharacterCount=MultiByteToWideChar(
        CP_UTF8,
        0,
        filePath.c_str(),
        static_cast<int>(filePath.size()),
        nullptr,
        0
    );
    if (wideCharacterCount<=0) {
        errorCodeOut=ERROR_INVALID_NAME;
        return PathAccessAnswer::CannotTell;
    }
    vector<wchar_t> widePath(static_cast<size_t>(wideCharacterCount) + 1, L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        filePath.c_str(),
        static_cast<int>(filePath.size()),
        widePath.data(),
        wideCharacterCount
    );
    const HANDLE fileHandle=CreateFileW(
        widePath.data(),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (fileHandle==INVALID_HANDLE_VALUE) {
        errorCodeOut=static_cast<int>(GetLastError());
        if (errorCodeOut==ERROR_FILE_NOT_FOUND || errorCodeOut==ERROR_PATH_NOT_FOUND) {
            return PathAccessAnswer::Missing;
        }
        if (errorCodeOut==ERROR_SHARING_VIOLATION || errorCodeOut==ERROR_LOCK_VIOLATION) {
            return PathAccessAnswer::InUse;
        }
        return PathAccessAnswer::CannotTell;
    }
    CloseHandle(fileHandle);
    return PathAccessAnswer::Free;
}
#else
// Asks the Unix lock manager whether `filePath` can carry a write lock. SQLite's
// locks live in the same `fcntl` lock space, so a successful request proves that
// no process, including a write ahead log connection, is holding it.
static PathAccessAnswer probePathAccess(const string& filePath, int& errorCodeOut) {
    // The descriptor is opened without `O_CREAT`, because this probe must never
    // bring a file into existence.
    int fileDescriptor=open(filePath.c_str(), O_RDWR);
    if (fileDescriptor<0 && errno==EACCES) {
        // A read only file cannot take a write lock, so the question is asked with
        // a read only descriptor instead, which still conflicts with a writer.
        fileDescriptor=open(filePath.c_str(), O_RDONLY);
    }
    if (fileDescriptor<0) {
        errorCodeOut=errno;
        return errno==ENOENT ? PathAccessAnswer::Missing : PathAccessAnswer::CannotTell;
    }
    // A length of zero with `SEEK_SET` at offset zero means "from the start of the
    // file to its end", which overlaps every lock SQLite could hold.
    struct flock lockRequest;
    memset(&lockRequest, 0, sizeof(lockRequest));
    lockRequest.l_type=F_WRLCK;
    lockRequest.l_whence=SEEK_SET;
    lockRequest.l_start=0;
    lockRequest.l_len=0;
    const bool lockAcquired=fcntl(fileDescriptor, F_SETLK, &lockRequest)==0;
    if (lockAcquired) {
        // The lock must be released at once. Leaving it held would make this tool
        // itself the thing blocking the coding agent, which is the exact failure
        // this class exists to prevent.
        struct flock releaseRequest;
        memset(&releaseRequest, 0, sizeof(releaseRequest));
        releaseRequest.l_type=F_UNLCK;
        releaseRequest.l_whence=SEEK_SET;
        releaseRequest.l_start=0;
        releaseRequest.l_len=0;
        fcntl(fileDescriptor, F_SETLK, &releaseRequest);
        close(fileDescriptor);
        return PathAccessAnswer::Free;
    }
    errorCodeOut=errno;
    close(fileDescriptor);
    return (errno==EACCES || errno==EAGAIN) ? PathAccessAnswer::InUse : PathAccessAnswer::CannotTell;
}
#endif
FileLockStatus FileLockDetector::checkLockStatus(const string& databaseFilePath) {
    FileLockStatus status;
    // Every file a live database may be holding is probed: the database itself and
    // the side files SQLite keeps beside it. The database is first, because a
    // missing database is a different answer from a held one.
    vector<string> pathsToProbe;
    pathsToProbe.push_back(databaseFilePath);
    for (const char* sideFileSuffix : {WRITE_AHEAD_LOG_SUFFIX, SHARED_MEMORY_SUFFIX, ROLLBACK_JOURNAL_SUFFIX}) {
        pathsToProbe.push_back(databaseFilePath + sideFileSuffix);
    }
    for (size_t pathIndex=0; pathIndex<pathsToProbe.size(); ++pathIndex) {
        int probeErrorCode=0;
        const PathAccessAnswer answer=probePathAccess(pathsToProbe[pathIndex], probeErrorCode);
        if (answer==PathAccessAnswer::Free) {
            continue;
        }
        if (answer==PathAccessAnswer::Missing) {
            if (pathIndex==0) {
                status.errorCode=probeErrorCode;
                status.explanation="The database file does not exist at that path.";
                return status;
            }
            // A side file that is not there cannot be held by anyone.
            continue;
        }
        if (answer==PathAccessAnswer::InUse) {
            status.isLocked=true;
            status.errorCode=probeErrorCode;
            status.explanation=pathIndex==0
                ? string("Another program has this database open.")
                : string("Another program has this database open: it is holding ")
                    + pathsToProbe[pathIndex] + ".";
            return status;
        }
        // The question could not be answered. For the database itself that is
        // reported rather than treated as free; for a side file it is ignored,
        // because the database itself was already proven free.
        if (pathIndex==0) {
            status.errorCode=probeErrorCode;
            status.explanation=Common::OperatingSystemErrorTranslator::explain(probeErrorCode);
            return status;
        }
    }
    status.errorCode=0;
    status.explanation="No other program has this database open.";
    return status;
}
void FileLockDetector::inspectCompanionFiles(const string& databaseFilePath, FileLockStatus& status) {
    // The write ahead log and the rollback journal sit next to the database with
    // a suffix appended to its name. Both are found with one metadata call each,
    // which is far cheaper than opening the database and asking SQLite.
    const string writeAheadLogPath=databaseFilePath + WRITE_AHEAD_LOG_SUFFIX;
    const FileMetadata writeAheadLogStatus=PlatformMetadata::readFileMetadata(writeAheadLogPath);
    if (writeAheadLogStatus.exists) {
        status.hasWriteAheadLog=true;
        status.companionFilePaths.push_back(writeAheadLogPath);
    }
    const string rollbackJournalPath=databaseFilePath + ROLLBACK_JOURNAL_SUFFIX;
    const FileMetadata rollbackJournalStatus=PlatformMetadata::readFileMetadata(rollbackJournalPath);
    if (rollbackJournalStatus.exists) {
        status.hasRollbackJournal=true;
        status.companionFilePaths.push_back(rollbackJournalPath);
    }
}
string FileLockDetector::describeWhoMightBeUsingIt(const vector<string>& likelyProcessNames) {
    if (likelyProcessNames.empty()) {
        return string("Close any program that might be using this coding agent, including its terminal window, and try again.");
    }
    // The names are joined into one sentence rather than printed as a list,
    // because the user needs one thing to do, not a menu.
    string joinedNames;
    for (size_t nameIndex=0; nameIndex<likelyProcessNames.size(); ++nameIndex) {
        if (nameIndex>0) {
            joinedNames+=nameIndex+1==likelyProcessNames.size() ? " or " : ", ";
        }
        joinedNames+=likelyProcessNames[nameIndex];
    }
    return string("Close ") + joinedNames
        + ", or any terminal window running it, and run the command again. Nothing has been changed yet.";
}
}
