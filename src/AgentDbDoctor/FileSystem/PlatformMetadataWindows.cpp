// The Windows implementation of the PlatformMetadata class.
//
// Every function here is defined once per platform so that the calling code
// never contains an operating system conditional. The Windows answers come from
// the wide character version of each call, because Windows paths are UTF-16 and
// a narrow character path cannot represent a user name with an accent in it.
#define WIN32_LEAN_AND_MEAN
// Declares the Win32 file, process, and console functions used below.
#include <windows.h>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, the growable array used for the wide character path buffer.
#include <vector>
// Provides `getenv`, which reads the process environment.
#include <cstdlib>
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
    // `WIN32_FILE_ATTRIBUTE_DATA` is a plain structure holding the answer to the
    // question. `GetFileAttributesExW` fills it in with one system call and does
    // not open the file.
    WIN32_FILE_ATTRIBUTE_DATA attributeData;
    // A `wchar_t` is a UTF-16 code unit on Windows. The conversion from the
    // UTF-8 `string` the rest of the tool uses happens here, and once per call,
    // which is why the caller never needs to know about it.
    const int wideCharacterCount=MultiByteToWideChar(
        CP_UTF8,
        0,
        filePath.c_str(),
        static_cast<int>(filePath.size()),
        nullptr,
        0
    );
    if (wideCharacterCount<=0) {
        result.errorCode=ERROR_INVALID_NAME;
        return result;
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
    if (!GetFileAttributesExW(widePath.data(), GetFileExInfoStandard, &attributeData)) {
        // The call failed. `GetLastError` reports why, and the value is kept so
        // the caller can tell "the file is not there" from "the file is there but
        // unreadable".
        result.errorCode=static_cast<int>(GetLastError());
        result.exists=result.errorCode!=ERROR_FILE_NOT_FOUND && result.errorCode!=ERROR_PATH_NOT_FOUND;
        return result;
    }
    result.exists=true;
    // `FILE_ATTRIBUTE_DIRECTORY` is a bit in the attribute mask. Testing a bit
    // with a bitwise and is how Windows flags are examined.
    result.isDirectory=(attributeData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)!=0;
    // The high and low halves of a 64 bit file size are stored in separate
    // 32 bit fields. `ULARGE_INTEGER` is a union that lets both halves be read
    // as one number.
    result.logicalSizeBytes=static_cast<long long>(attributeData.nFileSizeHigh) << 32
        | static_cast<long long>(attributeData.nFileSizeLow);
    // A Windows file time counts one hundred nanosecond intervals since 1601-01-01.
    // The Unix epoch is 1970-01-01, which is 11644473600 seconds later, so that
    // many seconds are subtracted to convert. The magic constant is the number of
    // seconds between the two dates, expressed in the same units as the field.
    constexpr long long SECONDS_BETWEEN_1601_AND_1970=11644473600LL;
    constexpr long long HUNDRED_NANOSECONDS_PER_MILLISECOND=10000LL;
    const unsigned long long windowsFileTime=
        (static_cast<unsigned long long>(attributeData.ftLastWriteTime.dwHighDateTime) << 32)
        | static_cast<unsigned long long>(attributeData.ftLastWriteTime.dwLowDateTime);
    result.modificationEpochMilliseconds=static_cast<long long>(
        (windowsFileTime / HUNDRED_NANOSECONDS_PER_MILLISECOND)
        - (SECONDS_BETWEEN_1601_AND_1970 * 1000LL));
    // `GetCompressedFileSizeW` reports how much space the file actually occupies,
    // which is different from its logical size when the file is compressed or
    // sparse. The high part of the answer comes back through a pointer, because a
    // sixty four bit result does not fit in a thirty two bit return value.
    DWORD highSizePart=0;
    const DWORD lowSizePart=GetCompressedFileSizeW(widePath.data(), &highSizePart);
    const DWORD compressedSizeError=GetLastError();
    // The documented failure case is `ERROR_INVALID_PARAMETER`, which means the
    // file is simply not compressed. In that case the allocated size is the
    // logical size, and treating the failure as a zero would be a lie.
    if (compressedSizeError==NO_ERROR || compressedSizeError==ERROR_INVALID_PARAMETER) {
        result.allocatedSizeBytes=
            (static_cast<long long>(highSizePart) << 32) | static_cast<long long>(lowSizePart);
    } else {
        // Any other failure has no better answer than the logical size, so the
        // report shows the number the user expects rather than a zero.
        result.allocatedSizeBytes=result.logicalSizeBytes;
    }
    return result;
}
DiskSpace PlatformMetadata::readDiskSpace(const string& pathOnVolume) {
    MetadataCallCounter::record();
    DiskSpace result;
    // The question is about a volume, and a volume is identified by a folder. When
    // the caller passed a file, its parent folder is used, so the answer is the same
    // whichever kind of path was given.
    string volumePath=pathOnVolume;
    const FileMetadata pathStatus=readFileMetadata(pathOnVolume);
    if (pathStatus.exists && !pathStatus.isDirectory) {
        const size_t lastSeparator=pathOnVolume.find_last_of("\\\\/");
        if (lastSeparator!=string::npos) {
            volumePath=pathOnVolume.substr(0, lastSeparator);
        }
    }
    const int wideCharacterCount=MultiByteToWideChar(
        CP_UTF8,
        0,
        volumePath.c_str(),
        static_cast<int>(volumePath.size()),
        nullptr,
        0
    );
    if (wideCharacterCount<=0) {
        result.errorCode=ERROR_INVALID_NAME;
        return result;
    }
    vector<wchar_t> widePath(static_cast<size_t>(wideCharacterCount) + 1, L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        volumePath.c_str(),
        static_cast<int>(volumePath.size()),
        widePath.data(),
        wideCharacterCount
    );
    // `ULARGE_INTEGER` is a union whose purpose is to let two 32 bit halves be
    // read as one 64 bit value. Free space can exceed four gigabytes, so the wide
    // form is required.
    ULARGE_INTEGER bytesAvailableToCaller;
    ULARGE_INTEGER totalBytesOnVolume;
    ULARGE_INTEGER totalFreeBytesOnVolume;
    if (!GetDiskFreeSpaceExW(widePath.data(), &bytesAvailableToCaller, &totalBytesOnVolume, &totalFreeBytesOnVolume)) {
        result.errorCode=static_cast<int>(GetLastError());
        return result;
    }
    result.isValid=true;
    result.availableBytes=static_cast<long long>(bytesAvailableToCaller.QuadPart);
    result.totalBytes=static_cast<long long>(totalBytesOnVolume.QuadPart);
    result.freeBytesTotal=static_cast<long long>(totalFreeBytesOnVolume.QuadPart);
    return result;
}
long long PlatformMetadata::metadataCallCount() {
    return MetadataCallCounter::callCount;
}
void PlatformMetadata::resetMetadataCallCount() {
    MetadataCallCounter::callCount=0;
}
long long PlatformMetadata::monotonicNanoseconds() {
    // `QueryPerformanceCounter` is the highest resolution counter available on
    // Windows. It counts intervals, not wall clock time, so a change to the system
    // clock cannot move it.
    LARGE_INTEGER counterValue;
    QueryPerformanceCounter(&counterValue);
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart==0) {
        return 0;
    }
    // The division converts counts into nanoseconds, but it is done in two steps.
    // Multiplying the raw counter by a billion would overflow on a machine that
    // has been up for more than a few hours, and the overflow would produce a
    // negative duration, which is the worst possible answer to a measurement.
    // Splitting the counter into whole seconds and a remainder keeps the largest
    // intermediate value small enough to be exact.
    const long long wholeSeconds=counterValue.QuadPart / frequency.QuadPart;
    const long long remainder=counterValue.QuadPart % frequency.QuadPart;
    return wholeSeconds * 1000000000LL
        + (remainder * 1000000000LL) / frequency.QuadPart;
}
void PlatformMetadata::sleepMilliseconds(int milliseconds) {
    if (milliseconds<=0) {
        return;
    }
    // `Sleep` blocks the calling thread for the given number of milliseconds. The
    // service is single threaded on purpose, so there is nothing else for the
    // thread to do while it waits.
    Sleep(static_cast<DWORD>(milliseconds));
}
int PlatformMetadata::currentProcessIdentifier() {
    // `GetCurrentProcessId` returns a pseudo handle free of charge, unlike
    // `GetCurrentProcess`, and the identifier is what is actually needed.
    return static_cast<int>(GetCurrentProcessId());
}
bool PlatformMetadata::isProcessRunning(int processIdentifier) {
    MetadataCallCounter::record();
    if (processIdentifier<=0) {
        return false;
    }
    // The three rights are the minimum needed to ask whether a process exists.
    // `PROCESS_QUERY_LIMITED_INFORMATION` was introduced in Windows Vista and is
    // granted to ordinary users, unlike the older `PROCESS_QUERY_INFORMATION`.
    const HANDLE processHandle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processIdentifier));
    if (processHandle==nullptr) {
        return false;
    }
    DWORD exitCode=0;
    const bool stillRunning=GetExitCodeProcess(processHandle, &exitCode) && exitCode==STILL_ACTIVE;
    // The handle must be closed whether or not the answer was found, which is
    // what the `if` plus explicit close expresses.
    CloseHandle(processHandle);
    return stillRunning;
}
string PlatformMetadata::hostName() {
    wchar_t hostBuffer[256];
    DWORD hostBufferSize=256;
    if (GetComputerNameW(hostBuffer, &hostBufferSize)) {
        // The conversion back to UTF-8 uses the explicit length form, because
        // `hostBuffer` is not null terminated in the way the narrow form assumes.
        const int byteCount=WideCharToMultiByte(
            CP_UTF8,
            0,
            hostBuffer,
            static_cast<int>(hostBufferSize),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (byteCount>0) {
            string hostNameText(static_cast<size_t>(byteCount), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                hostBuffer,
                static_cast<int>(hostBufferSize),
                hostNameText.data(),
                byteCount,
                nullptr,
                nullptr
            );
            return hostNameText;
        }
    }
    return string("unknown-host");
}
string PlatformMetadata::homeDirectory() {
    // `USERPROFILE` is the reliable source on Windows. `HOMEDRIVE` and
    // `HOMEPATH` are the older pair and are still honoured as a fallback because
    // some service accounts set only those.
    string userProfile;
    if (readEnvironmentVariable("USERPROFILE", userProfile) && !userProfile.empty()) {
        return userProfile;
    }
    string homeDrive;
    string homePath;
    if (readEnvironmentVariable("HOMEDRIVE", homeDrive) && readEnvironmentVariable("HOMEPATH", homePath)) {
        return homeDrive + homePath;
    }
    return string();
}
string PlatformMetadata::programDataDirectory() {
    string programData;
    if (readEnvironmentVariable("PROGRAMDATA", programData) && !programData.empty()) {
        return programData;
    }
    return string("C:\\ProgramData");
}
bool PlatformMetadata::isStandardOutputATerminal() {
    // `GetConsoleMode` succeeds only when the handle is a real console. When
    // output is redirected into a file or a pipe it fails, and that failure is
    // exactly the signal the presentation layer needs.
    const HANDLE standardOutputHandle=GetStdHandle(STD_OUTPUT_HANDLE);
    if (standardOutputHandle==nullptr || standardOutputHandle==INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD consoleMode=0;
    return GetConsoleMode(standardOutputHandle, &consoleMode)!=FALSE;
}
bool PlatformMetadata::readEnvironmentVariable(const string& variableName, string& valueOut) {
    // `_dupenv_s` reads the process environment, which is a copy made at start up.
    // It is the secure form of `getenv`, which the Windows compilers flag as
    // deprecated. It allocates one copy of the value, which the caller frees, so
    // this is not used on the hot path; the hot path never reads the environment.
    char* rawValuePointer=nullptr;
    size_t rawValueLength=0;
    if (_dupenv_s(&rawValuePointer, &rawValueLength, variableName.c_str())!=0) {
        return false;
    }
    if (rawValuePointer==nullptr) {
        return false;
    }
    const string copiedValue(rawValuePointer);
    free(rawValuePointer);
    if (copiedValue.empty()) {
        return false;
    }
    valueOut=copiedValue;
    return true;
}
}
