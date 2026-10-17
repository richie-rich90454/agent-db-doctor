// The plain English translation tables for SQLite result codes and for platform
// error numbers.
//
// Every raw number the tool ever encounters is converted to a sentence here, so
// the user never sees a bare code. The tables are lookup functions with a
// `switch` rather than a map, because a `switch` compiles into a jump table with
// no allocation and no hashing.
#include <string>
#include <string_view>
// Provides the SQLite result code constants.
#include <sqlite3.h>
// Provides the exception class declarations and the two translator declarations.
#include "AgentDbDoctor/Common/Exceptions.hpp"
using namespace std;
namespace AgentDbDoctor::Common {
// SQLite result codes that the tool reacts to by name. The values are taken from
// https://www.sqlite.org/rescode.html and are repeated here as named constants
// so that this file does not depend on the exact SQLite header layout.
namespace SqliteCode {
    // Extended codes add `value * 256` to the primary code.
    constexpr int EXTENDED_SHIFT=8;
    // SQLITE_IOERR_READ is the code the specification calls out: the operating
    // system refused to hand SQLite the bytes it asked for.
    constexpr int IOERR_READ=(10 | (1 << EXTENDED_SHIFT));
    // SQLITE_IOERR_WRITE means the write path failed rather than the read path.
    constexpr int IOERR_WRITE=(10 | (2 << EXTENDED_SHIFT));
    // SQLITE_IOERR_FSYNC means the flush to stable storage failed.
    constexpr int IOERR_FSYNC=(10 | (3 << EXTENDED_SHIFT));
    // SQLITE_IOERR_GETTEMPPATH means SQLite could not find a temporary directory.
    constexpr int IOERR_GETTEMPPATH=(10 | (6 << EXTENDED_SHIFT));
    // SQLITE_CORRUPT_VTAB is reported when a virtual table is damaged.
    constexpr int CORRUPT_VTAB=(11 | (1 << EXTENDED_SHIFT));
    // SQLITE_BUSY_SNAPSHOT is reported when a WAL snapshot changed mid read.
    constexpr int BUSY_SNAPSHOT=(5 | (1 << EXTENDED_SHIFT));
    // SQLITE_BUSY_RECOVERY is reported when a rollback journal is locked by a
    // recovery in progress.
    constexpr int BUSY_RECOVERY=(5 | (2 << EXTENDED_SHIFT));
    // SQLITE_READONLY_RECOVERY is reported when a hot journal is present.
    constexpr int READONLY_RECOVERY=(8 | (1 << EXTENDED_SHIFT));
    // SQLITE_READONLY_CANTINIT is reported when a shared cache cannot initialise.
    constexpr int READONLY_CANTINIT=(8 | (5 << EXTENDED_SHIFT));
    // SQLITE_CANTOPEN_NOTEMPDIR means the directory does not exist.
    constexpr int CANTOPEN_NOTEMPDIR=(14 | (1 << EXTENDED_SHIFT));
    // SQLITE_CANTOPEN_ISDIR means the path is a directory, not a file.
    constexpr int CANTOPEN_ISDIR=(14 | (2 << EXTENDED_SHIFT));
    // SQLITE_CANTOPEN_FULLPATH means the path could not be resolved.
    constexpr int CANTOPEN_FULLPATH=(14 | (3 << EXTENDED_SHIFT));
    // SQLITE_CONSTRAINT_FOREIGNKEY is raised by a foreign key violation.
    constexpr int CONSTRAINT_FOREIGNKEY=(19 | (3 << EXTENDED_SHIFT));
    // SQLITE_CONSTRAINT_NOTNULL is raised by a NOT NULL violation.
    constexpr int CONSTRAINT_NOTNULL=(19 | (5 << EXTENDED_SHIFT));
    // SQLITE_CONSTRAINT_PRIMARYKEY is raised by a primary key violation.
    constexpr int CONSTRAINT_PRIMARYKEY=(19 | (6 << EXTENDED_SHIFT));
    // SQLITE_CONSTRAINT_UNIQUE is raised by a unique index violation.
    constexpr int CONSTRAINT_UNIQUE=(19 | (8 << EXTENDED_SHIFT));
    // SQLITE_NOTICE_RECOVER_WAL is reported when a WAL was recovered on open.
    constexpr int NOTICE_RECOVER_WAL=(27 | (1 << EXTENDED_SHIFT));
    // SQLITE_NOTICE_RECOVER_ROLLBACK is reported when a rollback journal was
    // replayed on open.
    constexpr int NOTICE_RECOVER_ROLLBACK=(27 | (2 << EXTENDED_SHIFT));
    // SQLITE_WARNING_INDEX is reported when an index was missing.
    constexpr int WARNING_INDEX=(28 | (1 << EXTENDED_SHIFT));
    // SQLITE_ERROR_MISSING_COLLSEQ is reported when a collation is unavailable.
    constexpr int ERROR_MISSING_COLLSEQ=(1 | (1 << EXTENDED_SHIFT));
    // SQLITE_ERROR_SNAPSHOT is reported when a query is no longer valid.
    constexpr int ERROR_SNAPSHOT=(1 | (3 << EXTENDED_SHIFT));
}
string SqliteErrorTranslator::explain(int sqliteResultCode) {
    // The extended codes are checked first because they share their low byte
    // with a primary code. A switch on the full value handles both at once.
    switch (sqliteResultCode) {
        case SqliteCode::IOERR_READ: return "Error 266 (ERROR_READ_FAULT) - the operating system cannot read this file. This usually means the file was compressed or truncated while the database was open, or the drive is failing.";
        case SqliteCode::IOERR_WRITE: return "Error 522 (SQLITE_IOERR_WRITE) - SQLite could not write to the file. Check that the drive has free space and is writable.";
        case SqliteCode::IOERR_FSYNC: return "Error 778 (SQLITE_IOERR_FSYNC) - SQLite could not flush data to the drive. The drive may be failing or full.";
        case SqliteCode::IOERR_GETTEMPPATH: return "The system temporary directory could not be found. SQLite needs it to sort large results.";
        case SqliteCode::CORRUPT_VTAB: return "A virtual table inside the database is damaged. This is rare and usually means an extension created the table.";
        case SqliteCode::BUSY_SNAPSHOT: return "Another process changed the database while this read was in progress. Close the coding agent and try again.";
        case SqliteCode::BUSY_RECOVERY: return "Another process is recovering this database after an unclean shutdown. Wait for it to finish.";
        case SqliteCode::READONLY_RECOVERY: return "The database needs recovery before it can be opened for writing. Run the recover command to replay the journal.";
        case SqliteCode::READONLY_CANTINIT: return "SQLite could not initialise the write ahead log for this database.";
        case SqliteCode::CANTOPEN_NOTEMPDIR: return "One of the directories in the path does not exist.";
        case SqliteCode::CANTOPEN_ISDIR: return "The path points at a directory, not at a database file.";
        case SqliteCode::CANTOPEN_FULLPATH: return "The full path could not be resolved. Check that the path is correct.";
        case SqliteCode::CONSTRAINT_FOREIGNKEY: return "A foreign key rule would have been broken by this change.";
        case SqliteCode::CONSTRAINT_NOTNULL: return "A column that must always hold a value was given an empty value.";
        case SqliteCode::CONSTRAINT_PRIMARYKEY: return "A row with this primary key already exists.";
        case SqliteCode::CONSTRAINT_UNIQUE: return "A row with this value in a unique column already exists.";
        case SqliteCode::NOTICE_RECOVER_WAL: return "The write ahead log was recovered when the database was opened. This is normal after an unclean shutdown and is not a problem.";
        case SqliteCode::NOTICE_RECOVER_ROLLBACK: return "The rollback journal was replayed when the database was opened. This is normal after an unclean shutdown and is not a problem.";
        case SqliteCode::WARNING_INDEX: return "An index mentioned by the database is missing. This is usually harmless.";
        case SqliteCode::ERROR_MISSING_COLLSEQ: return "The database uses a text comparison rule this build of SQLite does not have.";
        case SqliteCode::ERROR_SNAPSHOT: return "This query is no longer valid because the data it read has changed. Run it again.";
        default: break;
    }
    // The remaining codes are the primary ones, which are small numbers.
    switch (sqliteResultCode) {
        case SQLITE_OK: return "No problem was reported.";
        case SQLITE_ERROR: return "SQLite ran into a problem it cannot describe further. The most common cause is a database file that is not a valid SQLite database.";
        case SQLITE_INTERNAL: return "SQLite found a bug in itself. This is not a problem with your data.";
        case SQLITE_PERM: return "You do not have permission to do this.";
        case SQLITE_ABORT: return "The operation was cancelled before it finished.";
        case SQLITE_BUSY: return "Another program is using this database right now. Close the coding agent and try again.";
        case SQLITE_LOCKED: return "A table in this database is locked by another process. Close the coding agent and try again.";
        case SQLITE_NOMEM: return "This program ran out of memory. Close other programs and try again.";
        case SQLITE_READONLY: return "The database can only be read, not written. This is normal for a database that has not been used yet.";
        case SQLITE_INTERRUPT: return "The operation was interrupted.";
        case SQLITE_IOERR: return "An input or output error occurred. The file may be compressed, on a disconnected network drive, or the drive may be failing.";
        case SQLITE_CORRUPT: return "The database file is damaged. The bytes in the file are not what SQLite expected.";
        case SQLITE_NOTFOUND: return "The requested row does not exist.";
        case SQLITE_FULL: return "The database or the drive is completely full. Free some space and try again.";
        case SQLITE_CANTOPEN: return "SQLite could not open the file.";
        case SQLITE_PROTOCOL: return "The database file is not a valid SQLite database. The file may be the wrong file, or it may be damaged.";
        case SQLITE_EMPTY: return "The database file is empty. Nothing has been written to it yet.";
        case SQLITE_SCHEMA: return "The structure of the database changed while this query was running. Run it again.";
        case SQLITE_TOOBIG: return "The result of this operation would be too large.";
        case SQLITE_CONSTRAINT: return "The change would break a rule the table declares, such as a unique or a not null rule.";
        case SQLITE_MISMATCH: return "The data types in this operation do not match what the table declares.";
        case SQLITE_MISUSE: return "This operation was used incorrectly.";
        case SQLITE_NOLFS: return "This build of SQLite cannot use the file system features this database needs.";
        case SQLITE_AUTH: return "The operation was refused by an authorisation callback.";
        case SQLITE_FORMAT: return "The database file was created by a newer version of SQLite and this build cannot read it.";
        case SQLITE_RANGE: return "A value was outside the range the operation accepts.";
        case SQLITE_NOTADB: return "The file is not a database. It may be a different kind of file, or a database from a different program.";
        case SQLITE_NOTICE: return "SQLite sent an informational notice.";
        case SQLITE_WARNING: return "SQLite sent a warning.";
        case SQLITE_ROW: return "A row is available. This is a normal step, not a failure.";
        case SQLITE_DONE: return "There are no more rows. This is a normal step, not a failure.";
        default: break;
    }
    // An unrecognised code still gets a complete sentence that names the number,
    // so the user can look it up rather than staring at a bare integer.
    return "SQLite reported error code " + to_string(sqliteResultCode) + ", which this version of the tool does not have a specific explanation for. See https://www.sqlite.org/rescode.html for the full list.";
}
string SqliteErrorTranslator::shortLabel(int sqliteResultCode) {
    switch (sqliteResultCode) {
        case SqliteCode::IOERR_READ: return "unreadable file";
        case SQLITE_CORRUPT: return "damaged file";
        case SQLITE_NOTADB: return "not a database";
        case SQLITE_BUSY: return "database in use";
        case SQLITE_LOCKED: return "table locked";
        case SQLITE_FULL: return "drive full";
        case SQLITE_READONLY: return "read only";
        case SQLITE_CANTOPEN: return "cannot open file";
        case SQLITE_PERM: return "permission denied";
        case SQLITE_NOMEM: return "out of memory";
        case SQLITE_PROTOCOL: return "invalid file";
        case SQLITE_EMPTY: return "empty file";
        case SQLITE_IOERR: return "input or output error";
        default: break;
    }
    return "error " + to_string(sqliteResultCode);
}
string OperatingSystemErrorTranslator::symbolicName(int operatingSystemErrorCode) {
#if defined(_WIN32)
    // The Windows symbolic names that matter for this tool. The values are from
    // the Win32 error list in the Windows SDK.
    switch (operatingSystemErrorCode) {
        case 1: return "ERROR_INVALID_FUNCTION";
        case 2: return "ERROR_FILE_NOT_FOUND";
        case 3: return "ERROR_PATH_NOT_FOUND";
        case 5: return "ERROR_ACCESS_DENIED";
        case 8: return "ERROR_NOT_ENOUGH_MEMORY";
        case 19: return "ERROR_NO_MEDIA";
        case 20: return "ERROR_BAD_UNIT";
        case 21: return "ERROR_NOT_READY";
        case 23: return "ERROR_CRC";
        case 24: return "ERROR_BAD_LENGTH";
        case 30: return "ERROR_READ_FAULT";
        case 31: return "ERROR_WRITE_FAULT";
        case 32: return "ERROR_SHARING_VIOLATION";
        case 33: return "ERROR_LOCK_VIOLATION";
        case 34: return "ERROR_SEEK";
        case 36: return "ERROR_NO_MORE_FILES";
        case 39: return "ERROR_HANDLE_DISK_FULL";
        case 112: return "ERROR_DISK_FULL";
        case 1117: return "ERROR_IO_PENDING";
        case 1224: return "ERROR_USER_MAPPED_FILE";
        case 1397: return "ERROR_FILE_CORRUPT";
        case 1450: return "ERROR_DISK_QUOTA_EXCEEDED";
        case 1451: return "ERROR_QUOTA_EXCEEDED";
        case 1920: return "ERROR_CANT_ACCESS_FILE";
        case 1921: return "ERROR_CANT_RESOLVE_FILENAME";
        default: break;
    }
    return "WIN32_ERROR_" + to_string(operatingSystemErrorCode);
#else
    // On Linux and macOS the numbers come from the `errno` variable. The names
    // are the symbolic constants without the `E` prefix.
    switch (operatingSystemErrorCode) {
        case 1: return "EPERM";
        case 2: return "ENOENT";
        case 5: return "EIO";
        case 9: return "EBADF";
        case 11: return "EAGAIN";
        case 12: return "ENOMEM";
        case 13: return "EACCES";
        case 16: return "EBUSY";
        case 21: return "EISDIR";
        case 22: return "EINVAL";
        case 23: return "ENFILE";
        case 24: return "EMFILE";
        case 26: return "ETXTBSY";
        case 27: return "EFBIG";
        case 28: return "ENOSPC";
        case 30: return "EROFS";
        case 32: return "EPIPE";
        case 36: return "ENAMETOOLONG";
        case 40: return "ELOOP";
        default: break;
    }
    return "ERRNO_" + to_string(operatingSystemErrorCode);
#endif
}
string OperatingSystemErrorTranslator::explain(int operatingSystemErrorCode) {
    // The symbolic name is always included, because a named code is far easier
    // to search for than a bare number.
    const string name=symbolicName(operatingSystemErrorCode);
#if defined(_WIN32)
    switch (operatingSystemErrorCode) {
        case 2: return "Error 2 (ERROR_FILE_NOT_FOUND) - the file does not exist at that path. The coding agent may store its data somewhere else, or may not have been used yet.";
        case 3: return "Error 3 (ERROR_PATH_NOT_FOUND) - one of the folders in the path does not exist.";
        case 5: return "Error 5 (ERROR_ACCESS_DENIED) - this program is not allowed to touch that file. Run the tool as the same user who owns the coding agent, or as an administrator.";
        case 30: return "Error 30 (ERROR_READ_FAULT) - the operating system cannot read this file. This usually means the file was compressed or truncated while the database was open.";
        case 31: return "Error 31 (ERROR_WRITE_FAULT) - the operating system cannot write to this file. The drive may be full, read only, or failing.";
        case 32: return "Error 32 (ERROR_SHARING_VIOLATION) - another program has this file open. Close the coding agent and try again.";
        case 33: return "Error 33 (ERROR_LOCK_VIOLATION) - another program holds a lock on this file. Close the coding agent and try again.";
        case 34: return "Error 34 (ERROR_SEEK) - the file pointer could not be moved. The drive may be failing.";
        case 39: return "Error 39 (ERROR_HANDLE_DISK_FULL) - the drive is completely full. Free some space and try again.";
        case 112: return "Error 112 (ERROR_DISK_FULL) - the drive is completely full. Free some space and try again.";
        case 1224: return "Error 1224 (ERROR_USER_MAPPED_FILE) - Windows has this file open in memory. Close every program that might be using it, including the coding agent, and try again.";
        case 1397: return "Error 1397 (ERROR_FILE_CORRUPT) - Windows considers this file damaged. The file system metadata for it no longer matches the file itself.";
        case 1450: return "Error 1450 (ERROR_DISK_QUOTA_EXCEEDED) - the disk quota for this user is used up. Delete some files or raise the quota.";
        case 1451: return "Error 1451 (ERROR_QUOTA_EXCEEDED) - the disk quota for this user is used up. Delete some files or raise the quota.";
        default: break;
    }
#else
    switch (operatingSystemErrorCode) {
        case 1: return "Error 1 (EPERM) - you do not have permission to do this.";
        case 2: return "Error 2 (ENOENT) - the file does not exist at that path. The coding agent may store its data somewhere else, or may not have been used yet.";
        case 5: return "Error 5 (EIO) - a hardware or device error occurred while reading or writing. The drive may be failing.";
        case 11: return "Error 11 (EAGAIN) - the resource is temporarily busy. Wait a moment and try again.";
        case 12: return "Error 12 (ENOMEM) - this program ran out of memory. Close other programs and try again.";
        case 13: return "Error 13 (EACCES) - this program is not allowed to touch that file. Run the tool as the user who owns the coding agent, or with administrator rights.";
        case 16: return "Error 16 (EBUSY) - the file is in use by another program. Close the coding agent and try again.";
        case 22: return "Error 22 (EINVAL) - an argument passed to the system call was not valid.";
        case 27: return "Error 27 (EFBIG) - the file or the operation is larger than this system allows.";
        case 28: return "Error 28 (ENOSPC) - the drive is completely full. Free some space and try again.";
        case 30: return "Error 30 (EROFS) - the file system is mounted read only, so nothing can be written to it.";
        case 32: return "Error 32 (EPIPE) - the connection to the file was closed unexpectedly.";
        case 36: return "Error 36 (ENAMETOOLONG) - the path is too long for this system.";
        case 40: return "Error 40 (ELOOP) - the path contains a loop of symbolic links.";
        default: break;
    }
#endif
    return "The operating system reported " + name + ", which this version of the tool does not have a specific explanation for. The code itself is " + to_string(operatingSystemErrorCode) + ".";
}
}
