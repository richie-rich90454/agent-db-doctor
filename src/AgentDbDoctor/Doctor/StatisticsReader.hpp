// The StatisticsReader class, which answers questions about a database without
// reading more of it than it must.
//
// This is the single most important performance class in the codebase. Every
// question a diagnostic pass asks has a constant time answer somewhere in
// SQLite's own bookkeeping: the page size and page count live in the hundred
// byte header of page one, and the approximate row counts live in a table SQLite
// maintains. Running `SELECT COUNT(*)` on the event table of a hundred gigabyte
// database would read the entire file and could take minutes. Reading one row
// from each of those two places takes a handful of system calls.
#ifndef AGENT_DB_DOCTOR_DOCTOR_STATISTICSREADER_HPP
#define AGENT_DB_DOCTOR_DOCTOR_STATISTICSREADER_HPP
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `string_view`, a read only view of characters owned elsewhere.
#include <string_view>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Persistence {
// A forward declaration of the Database class, so that this header does not have
// to include the whole SQLite wrapper. A forward declaration names a class
// without defining it, which lets a pointer or reference to it appear here. The
// methods below are only defined in the implementation file, where the full type
// is available.
class Database;
}
// The methods below name that class from the doctor namespace, so its namespace is
// brought into scope here. The directive sits after the declaration above, because
// that is where the namespace exists.
using namespace AgentDbDoctor::Persistence;
namespace AgentDbDoctor::Doctor {
// The SqliteHeaderFields value type holds the fields SQLite keeps in the header
// of the first page of a database file.
//
// The layout is documented at https://www.sqlite.org/fileformat.html and is fixed
// for every version of the file format, which is what makes it safe to read the
// bytes directly instead of asking SQLite.
struct SqliteHeaderFields final {
    // True when the first sixteen bytes are the SQLite signature, which proves
    // the file is a SQLite database and not something else.
    bool isValidSqliteFile=false;
    // The page size in bytes, for example 4096. A value of one in the file means
    // sixty five thousand five hundred and thirty six, which this class resolves.
    long long pageSizeBytes=0;
    // The number of pages in the database, as of the last write.
    long long pageCount=0;
    // The number of pages on the free list. These are pages that were used once
    // and released, and they are exactly the space a compaction would give back.
    long long freePageCount=0;
    // The text encoding SQLite uses. One is UTF-8, two is UTF-16 little endian,
    // and three is UTF-16 big endian.
    long long textEncodingCode=0;
    // The user version, an integer the application may use for its own purposes.
    long long userVersion=0;
    // The schema format number, which rises as SQLite's features are used.
    long long schemaFormatNumber=0;
    // The size of the database implied by the page count and the page size. This
    // is what SQLite believes the file size should be, which can differ from what
    // the file system reports when a write was interrupted.
    long long declaredFileSizeBytes=0;
};
// The StatisticsReader class reads row counts and header fields in constant time.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state at all, which is why every method is `static` and the class has a deleted
// constructor.
class StatisticsReader final {
public:
    StatisticsReader()=delete;
    // Reads the hundred byte header of the first page of the database at
    // `databaseFilePath`.
    //
    // This performs one file open, one read of one hundred bytes, and one close.
    // It does not go through SQLite at all, which means it still works when the
    // database is damaged, and it takes no lock, so it is safe to run while the
    // coding agent has the file open. A short or unreadable file yields a
    // structure whose `isValidSqliteFile` is false rather than an exception,
    // because a damaged database is an expected diagnostic result rather than a
    // programming error.
    static SqliteHeaderFields readHeaderFields(const string& databaseFilePath);
    // Returns an approximate row count for the named table.
    //
    // The value comes from the `sqlite_stat1` table, which SQLite populates when
    // the user runs `ANALYZE`. Reading one row from it is constant time. If the
    // table is missing, or the named table is not in it, this returns `nullopt`
    // and the caller falls back to an exact count. The fallback is slower but is
    // only ever used for small tables, never for the event table.
    static optional<long> approximateRowCount(Database& database, string_view tableName);
    // True when a table with the given name exists. The answer comes from
    // SQLite's own schema table, so it costs one indexed lookup.
    static bool tableExists(Database& database, string_view tableName);
    // True when a column with the given name exists on the given table. Used to
    // check the shape of the schema before a query that depends on it.
    static bool columnExists(Database& database, string_view tableName, string_view columnName);
    // The number of rows in a table, counted exactly.
    //
    // This walks the whole table and is therefore linear in the number of rows.
    // The comment on every call site says why it is acceptable there: it is only
    // ever used for the small tables, and only when the constant time answer was
    // unavailable.
    static long long exactRowCount(Database& database, string_view tableName);
    // The number of pages the database actually occupies, according to SQLite,
    // as opposed to what the file system reports. Returns zero when the query
    // fails.
    static long long pageCountFromEngine(Database& database);
    // The number of free pages according to SQLite's engine rather than the file
    // header. Returns zero when the query fails.
    static long long freePageCountFromEngine(Database& database);
    // The number of bytes a compaction would give back, which is the free page
    // count multiplied by the page size. Returns zero when either is unknown.
    static long long reclaimableBytesFromFreeList(const SqliteHeaderFields& headerFields);
private:
};
}
#endif
