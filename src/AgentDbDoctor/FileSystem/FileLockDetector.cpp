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
#include <string>
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
// The rollback journal, which exists only when a write was interrupted.
static constexpr const char* ROLLBACK_JOURNAL_SUFFIX="-journal";
FileLockStatus FileLockDetector::checkLockStatus(const string& databaseFilePath) {
    FileLockStatus status;
#if defined(_WIN32)
    // `CreateFileW` with a share mode of zero asks for exclusive access. If any
    // other process has the file open, Windows refuses with
    // ERROR_SHARING_VIOLATION, and that refusal is the answer.
    const int wideCharacterCount=MultiByteToWideChar(
        CP_UTF8,
        0,
        databaseFilePath.c_str(),
        static_cast<int>(databaseFilePath.size()),
        nullptr,
        0
    );
    if (wideCharacterCount<=0) {
        status.explanation="The path could not be understood.";
        return status;
    }
    vector<wchar_t> widePath(static_cast<size_t>(wideCharacterCount) + 1, L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        databaseFilePath.c_str(),
        static_cast<int>(databaseFilePath.size()),
        widePath.data(),
        wideCharacterCount
    );
    // `GENERIC_READ` with no sharing is the least privilege that still answers
    // the question. The handle is opened with `FILE_ATTRIBUTE_NORMAL` because
    // the file may be compressed or sparse and those attributes are harmless.
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
        const int failureCode=static_cast<int>(GetLastError());
        status.errorCode=failureCode;
        status.isLocked=failureCode==ERROR_SHARING_VIOLATION || failureCode==ERROR_LOCK_VIOLATION;
        if (status.isLocked) {
            status.explanation="Another program has this database open.";
        } else if (failureCode==ERROR_FILE_NOT_FOUND) {
            status.explanation="The database file does not exist at that path.";
        } else {
            status.explanation=AgentDbDoctor::Common::OperatingSystemErrorTranslator::explain(failureCode);
        }
        return status;
    }
    // The handle was opened with exclusive access, which proves no other process
    // has the file open. It must be closed on every path, which is what the
    // single close call here expresses.
    CloseHandle(fileHandle);
    status.explanation="No other program has this database open.";
    return status;
#else
    // On Unix the only reliable question is whether a conflicting lock can be
    // taken. SQLite takes its locks with `fcntl` byte range locks, and `fcntl`
    // locks occupy the same lock space, so asking for a write lock over the whole
    // file conflicts with any process that has the database open.
    //
    // `flock` is deliberately not used. On Linux the `flock` and `fcntl` lock
    // spaces are independent, so a `flock` test would succeed even while SQLite
    // held the file, and the tool would happily prune a database the coding agent
    // was writing to.
    //
    // The descriptor is opened without `O_CREAT`, because this probe must never
    // bring a file into existence.
    int fileDescriptor=open(databaseFilePath.c_str(), O_RDWR);
    if (fileDescriptor<0 && errno==EACCES) {
        // A read only file cannot take a write lock, so the question is asked with
        // a read lock instead. That still detects a writer, which is the case that
        // matters most.
        fileDescriptor=open(databaseFilePath.c_str(), O_RDONLY);
    }
    if (fileDescriptor<0) {
        status.errorCode=errno;
        status.explanation=errno==ENOENT
            ? string("The database file does not exist at that path.")
            : AgentDbDoctor::Common::AgentDbDoctor::Common::OperatingSystemErrorTranslator::explain(errno);
        return status;
    }
    // `struct flock` describes one byte range lock. A length of zero with
    // `SEEK_SET` at offset zero means "from the start of the file to its end",
    // which is what makes the request overlap every lock SQLite could hold.
    struct flock lockRequest;
    memset(&lockRequest, 0, sizeof(lockRequest));
    lockRequest.l_type=F_WRLCK;
    lockRequest.l_whence=SEEK_SET;
    lockRequest.l_start=0;
    lockRequest.l_len=0;
    // `F_SETLK` sets the lock without waiting. `F_SETLKW` would block until the
    // other process let go, and a tool that blocks is a tool the user has to kill.
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
    }
    // The descriptor must be closed on every path, which is what the single close
    // call here expresses.
    close(fileDescriptor);
    status.isLocked=!lockAcquired;
    status.errorCode=status.isLocked ? errno : 0;
    status.explanation=status.isLocked
        ? string("Another program has this database open.")
        : string("No other program has this database open.");
    return status;
#endif
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
