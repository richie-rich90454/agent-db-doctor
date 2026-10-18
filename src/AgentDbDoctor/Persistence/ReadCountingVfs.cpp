// The implementation of the ReadCountingVfs class.
//
// The structure of the wrapper is the standard one for a SQLite file system that
// wants to observe without changing behaviour. The whole virtual file system
// structure of the default is copied and one function, `xOpen`, is replaced. That
// function asks the copied file system for the real file object, places it
// immediately behind a small header of this wrapper's own, and returns a file whose
// method table is this wrapper's. Every method then delegates to the file behind
// the header, and only `xRead` adds anything: the byte and call counters.
//
// Nothing is reordered, cached, or answered differently from what SQLite's own
// file system would say, so a connection opened through the wrapper behaves
// exactly as it did before the wrapper existed.
// Provides the raw SQLite C API, including both structure definitions.
#include <sqlite3.h>
// Provides `memcpy`, which copies the default file system structure.
#include <cstring>
// Provides the ReadCountingVfs class declaration.
#include "AgentDbDoctor/Persistence/ReadCountingVfs.hpp"
using namespace std;
namespace AgentDbDoctor::Persistence {
namespace {
// The counters. They are plain variables rather than atomics because the vendored
// SQLite is compiled with `SQLITE_THREADSAFE=0` and the tool is single threaded by
// design, which is the same reason the rest of the codebase has none.
long long countedByteTotal=0;
long long countedCallTotal=0;
// The default file system as it was before the wrapper was installed. It stays
// valid for the process, because SQLite keeps its built in file systems registered.
sqlite3_vfs* originalVfs=nullptr;
// The copy of the default file system that is registered as the new default.
sqlite3_vfs wrapperVfs;
// True once the wrapper is registered.
bool wrapperIsInstalled=false;
// The header this wrapper puts in front of the real file object. The real object
// lives immediately after the header, inside the same allocation, which is why the
// wrapper's `szOsFile` is the original size plus this structure.
struct CountingFile final {
    sqlite3_file header;
};
// The real file object: the one the default file system created for us.
sqlite3_file* fileBehind(sqlite3_file* file) {
    return reinterpret_cast<sqlite3_file*>(reinterpret_cast<CountingFile*>(file) + 1);
}
int countingClose(sqlite3_file* file) {
    sqlite3_file* innerFile=fileBehind(file);
    const int result=innerFile->pMethods->xClose(innerFile);
    file->pMethods=nullptr;
    return result;
}
int countingRead(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset) {
    sqlite3_file* innerFile=fileBehind(file);
    const int result=innerFile->pMethods->xRead(innerFile, buffer, amount, offset);
    // Only a read that delivered its bytes is counted. A short read at the end of
    // the file reports a partial result, and counting the request rather than the
    // delivery would overstate what the tool actually read.
    if (result==SQLITE_OK && amount>0) {
        countedByteTotal+=amount;
        countedCallTotal++;
    }
    return result;
}
int countingWrite(sqlite3_file* file, const void* buffer, int amount, sqlite3_int64 offset) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xWrite(innerFile, buffer, amount, offset);
}
int countingTruncate(sqlite3_file* file, sqlite3_int64 size) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xTruncate(innerFile, size);
}
int countingSync(sqlite3_file* file, int flags) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xSync(innerFile, flags);
}
int countingFileSize(sqlite3_file* file, sqlite3_int64* sizeOut) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xFileSize(innerFile, sizeOut);
}
int countingLock(sqlite3_file* file, int lockType) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xLock(innerFile, lockType);
}
int countingUnlock(sqlite3_file* file, int lockType) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xUnlock(innerFile, lockType);
}
int countingCheckReservedLock(sqlite3_file* file, int* resultOut) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xCheckReservedLock(innerFile, resultOut);
}
int countingFileControl(sqlite3_file* file, int operation, void* argument) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xFileControl(innerFile, operation, argument);
}
int countingSectorSize(sqlite3_file* file) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xSectorSize(innerFile);
}
int countingDeviceCharacteristics(sqlite3_file* file) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xDeviceCharacteristics(innerFile);
}
int countingShmMap(sqlite3_file* file, int region, int pageSize, int extend, void volatile** mappedRegionOut) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xShmMap(innerFile, region, pageSize, extend, mappedRegionOut);
}
int countingShmLock(sqlite3_file* file, int offset, int count, int flags) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xShmLock(innerFile, offset, count, flags);
}
void countingShmBarrier(sqlite3_file* file) {
    sqlite3_file* innerFile=fileBehind(file);
    innerFile->pMethods->xShmBarrier(innerFile);
}
int countingShmUnmap(sqlite3_file* file, int deleteFlag) {
    sqlite3_file* innerFile=fileBehind(file);
    return innerFile->pMethods->xShmUnmap(innerFile, deleteFlag);
}
int countingFetch(sqlite3_file* file, sqlite3_int64 offset, int amount, void** pageOut) {
    sqlite3_file* innerFile=fileBehind(file);
    // The default file systems do not implement `xFetch`, and SQLite would never
    // call it on a file that did not advertise it. The wrapper advertises it, so a
    // missing implementation is answered the way SQLite itself answers it when a
    // file system cannot fetch: the page is simply not cached, and the normal read
    // path is used.
    if (innerFile->pMethods->xFetch==nullptr) {
        *pageOut=nullptr;
        return SQLITE_OK;
    }
    return innerFile->pMethods->xFetch(innerFile, offset, amount, pageOut);
}
int countingUnfetch(sqlite3_file* file, sqlite3_int64 offset, void* page) {
    sqlite3_file* innerFile=fileBehind(file);
    if (innerFile->pMethods->xUnfetch==nullptr) {
        return SQLITE_OK;
    }
    return innerFile->pMethods->xUnfetch(innerFile, offset, page);
}
// The method table handed out by the wrapper. Its version is three, the newest
// SQLite defines, and every function above exists.
const sqlite3_io_methods COUNTING_IO_METHODS={
    3,
    countingClose,
    countingRead,
    countingWrite,
    countingTruncate,
    countingSync,
    countingFileSize,
    countingLock,
    countingUnlock,
    countingCheckReservedLock,
    countingFileControl,
    countingSectorSize,
    countingDeviceCharacteristics,
    countingShmMap,
    countingShmLock,
    countingShmBarrier,
    countingShmUnmap,
    countingFetch,
    countingUnfetch
};
int countingOpen(sqlite3_vfs* vfs, sqlite3_filename name, sqlite3_file* file, int flags, int* flagsOut) {
    // The vfs parameter is the wrapper itself, which is a copy, so the original
    // file system is called through its own pointer instead.
    (void)vfs;
    if (originalVfs==nullptr) {
        return SQLITE_CANTOPEN;
    }
    CountingFile* countingFile=reinterpret_cast<CountingFile*>(file);
    sqlite3_file* innerFile=fileBehind(file);
    const int openResult=originalVfs->xOpen(originalVfs, name, innerFile, flags, flagsOut);
    if (openResult!=SQLITE_OK) {
        return openResult;
    }
    countingFile->header.pMethods=&COUNTING_IO_METHODS;
    return SQLITE_OK;
}
}
void ReadCountingVfs::install() {
    if (wrapperIsInstalled) {
        return;
    }
    sqlite3_vfs* currentDefault=sqlite3_vfs_find(nullptr);
    if (currentDefault==nullptr) {
        return;
    }
    originalVfs=currentDefault;
    // The whole structure is copied, then the two fields that must differ are
    // replaced: the name, the open function, and the size of the file object the
    // file system needs, which is the original size plus this wrapper's header.
    memcpy(&wrapperVfs, currentDefault, sizeof(sqlite3_vfs));
    wrapperVfs.zName="agent-db-doctor-counting";
    wrapperVfs.pNext=nullptr;
    wrapperVfs.szOsFile=static_cast<int>(sizeof(CountingFile))+currentDefault->szOsFile;
    wrapperVfs.xOpen=countingOpen;
    // The second argument makes the wrapper the default, so a connection that names
    // no file system, which is every connection this tool opens, uses it.
    sqlite3_vfs_register(&wrapperVfs, 1);
    wrapperIsInstalled=true;
}
void ReadCountingVfs::resetCounters() {
    countedByteTotal=0;
    countedCallTotal=0;
}
long long ReadCountingVfs::countedBytes() {
    return countedByteTotal;
}
long long ReadCountingVfs::countedReadCalls() {
    return countedCallTotal;
}
bool ReadCountingVfs::isInstalled() {
    return wrapperIsInstalled;
}
}
