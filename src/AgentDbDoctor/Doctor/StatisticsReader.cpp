// The implementation of the StatisticsReader class.
//
// The header read here is the only place the tool reads bytes from a database
// outside SQLite itself, and it reads exactly one hundred of them. Everything
// else about a database's size is answered from the file system's own metadata.
#include <cstdio>
// Provides `from_chars`, the standard library integer parser that allocates
// nothing, and `errc`, the error code it reports.
#include <charconv>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `string_view`, a read only view of characters owned elsewhere.
#include <string_view>
// Provides the StatisticsReader class and the header field structure.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Persistence;
namespace AgentDbDoctor::Doctor {
// The number of bytes of the database header this class reads. The header is a
// hundred bytes long, and the fields that matter all sit inside it.
static constexpr size_t SQLITE_HEADER_BYTE_COUNT=100;
// The magic string every SQLite database begins with, without its terminating
// null. Comparing these sixteen bytes is how the tool tells a database from a
// text file without opening SQLite.
static constexpr const char* SQLITE_FILE_SIGNATURE="SQLite format 3";
// The number of characters in that signature.
static constexpr size_t SQLITE_FILE_SIGNATURE_LENGTH=16;
// The file format writes the page size as one would encode the number one when
// the real page size is sixty five thousand five hundred and thirty six, which is
// the largest page size SQLite supports and cannot be written in two bytes.
static constexpr long long SQLITE_MAXIMUM_PAGE_SIZE=65536;
// Reads a big endian sixteen bit unsigned integer from a byte buffer.
//
// SQLite writes every integer in its file header in big endian order, meaning the
// most significant byte first, which is the opposite of every x86 processor's
// native order. Shifting and masking each byte into place is how that is done
// without a conversion function.
static long long readBigEndianUnsignedShort(const unsigned char* byteBuffer, size_t byteOffset) {
    return (static_cast<long long>(byteBuffer[byteOffset]) << 8)
        | static_cast<long long>(byteBuffer[byteOffset + 1]);
}
// Reads a big endian thirty two bit unsigned integer from a byte buffer.
static long long readBigEndianUnsignedInt(const unsigned char* byteBuffer, size_t byteOffset) {
    return (static_cast<long long>(byteBuffer[byteOffset]) << 24)
        | (static_cast<long long>(byteBuffer[byteOffset + 1]) << 16)
        | (static_cast<long long>(byteBuffer[byteOffset + 2]) << 8)
        | static_cast<long long>(byteBuffer[byteOffset + 3]);
}
SqliteHeaderFields StatisticsReader::readHeaderFields(const string& databaseFilePath) {
    SqliteHeaderFields headerFields;
    // The file is opened in binary read mode. On Windows the text mode would
    // rewrite line endings, which would corrupt the bytes being measured.
    FILE* databaseFile=fopen(databaseFilePath.c_str(), "rb");
    if (databaseFile==nullptr) {
        return headerFields;
    }
    // The buffer is a fixed size array on the stack, so no allocation happens.
    // Only the first hundred bytes are ever read, and the rest of the array is
    // never touched.
    unsigned char headerBytes[SQLITE_HEADER_BYTE_COUNT];
    // The result of `fread` is the number of bytes actually read, which is less
    // than requested when the file is shorter than the header. A short read is
    // exactly the signal that the file is truncated.
    const size_t bytesRead=fread(headerBytes, 1, SQLITE_HEADER_BYTE_COUNT, databaseFile);
    // The handle is closed on every path, including the early return below, which
    // is why the read and the checks are separated by a single close call.
    fclose(databaseFile);
    if (bytesRead<SQLITE_FILE_SIGNATURE_LENGTH) {
        return headerFields;
    }
    // The signature comparison is the check that decides whether this is a
    // database at all. A file that is shorter than sixteen bytes cannot be one,
    // because every database begins with the signature.
    bool signatureMatches=true;
    for (size_t signatureIndex=0; signatureIndex<SQLITE_FILE_SIGNATURE_LENGTH; ++signatureIndex) {
        if (headerBytes[signatureIndex]!=static_cast<unsigned char>(SQLITE_FILE_SIGNATURE[signatureIndex])) {
            signatureMatches=false;
            break;
        }
    }
    if (!signatureMatches) {
        return headerFields;
    }
    headerFields.isValidSqliteFile=true;
    if (bytesRead<SQLITE_HEADER_BYTE_COUNT) {
        // The signature is present but the header is truncated, so the file is
        // damaged. The page size is still readable at bytes sixteen and seventeen,
        // because the signature occupies bytes zero through fifteen.
        headerFields.pageSizeBytes=readBigEndianUnsignedShort(headerBytes, 16);
        if (headerFields.pageSizeBytes==1) {
            headerFields.pageSizeBytes=SQLITE_MAXIMUM_PAGE_SIZE;
        }
        return headerFields;
    }
    // Byte sixteen is the high half of the page size. The value one is the escape
    // code for the largest page size, which does not fit in two bytes.
    headerFields.pageSizeBytes=readBigEndianUnsignedShort(headerBytes, 16);
    if (headerFields.pageSizeBytes==1) {
        headerFields.pageSizeBytes=SQLITE_MAXIMUM_PAGE_SIZE;
    }
    // Bytes twenty eight through thirty one hold the size of the database in
    // pages. A value of zero means the file is empty.
    headerFields.pageCount=readBigEndianUnsignedInt(headerBytes, 28);
    // Bytes thirty six through thirty nine hold the number of pages on the free
    // list. These are pages that were allocated once and then released, and they
    // are the space a compaction would return to the file system.
    headerFields.freePageCount=readBigEndianUnsignedInt(headerBytes, 36);
    // Bytes forty four through forty seven hold the schema format number.
    headerFields.schemaFormatNumber=readBigEndianUnsignedInt(headerBytes, 44);
    // Bytes fifty six through fifty nine hold the text encoding.
    headerFields.textEncodingCode=readBigEndianUnsignedInt(headerBytes, 56);
    // Bytes sixty through sixty three hold the user version.
    headerFields.userVersion=readBigEndianUnsignedInt(headerBytes, 60);
    headerFields.declaredFileSizeBytes=headerFields.pageCount * headerFields.pageSizeBytes;
    return headerFields;
}
optional<long> StatisticsReader::approximateRowCount(Database& database, string_view tableName) {
    // `sqlite_stat1` exists only after an `ANALYZE` has been run. If it is absent
    // the prepare fails, which this method reports as "no answer available" rather
    // than as an error, because the caller has a documented fallback.
    Statement statisticsStatement(
        database.rawHandle(),
        "SELECT stat FROM sqlite_stat1 WHERE tbl=?1 LIMIT 1",
        "the approximate row count for a table"
    );
    if (!statisticsStatement.isValid()) {
        return nullopt;
    }
    statisticsStatement.bindText(1, tableName);
    if (!statisticsStatement.step()) {
        return nullopt;
    }
    const string_view statisticsText=statisticsStatement.columnTextView(0);
    if (statisticsText.empty()) {
        return nullopt;
    }
    // The `stat` column holds space separated numbers. The first is the number of
    // rows, the second the number of distinct values in the first index column,
    // and the third the number of distinct values overall. Only the first is
    // wanted, so everything from the first space onward is ignored.
    const size_t spaceIndex=statisticsText.find(' ');
    const string_view rowCountText=spaceIndex==string_view::npos
        ? statisticsText
        : statisticsText.substr(0, spaceIndex);
    // The characters are converted to a `long` here because `from_chars` is the
    // fastest integer parser in the standard library and the field is always an
    // integer. Anything malformed yields no value rather than a wrong one.
    long parsedRowCount=0;
    const auto parseResult=from_chars(
        rowCountText.data(),
        rowCountText.data() + rowCountText.size(),
        parsedRowCount
    );
    if (parseResult.ec!=errc{}) {
        return nullopt;
    }
    return parsedRowCount;
}
bool StatisticsReader::tableExists(Database& database, string_view tableName) {
    // Querying SQLite's own schema table is an indexed lookup on the table name,
    // so it costs the same whether the database holds ten rows or a hundred
    // million.
    Statement schemaStatement(
        database.rawHandle(),
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1",
        "a check for whether a table exists"
    );
    if (!schemaStatement.isValid()) {
        return false;
    }
    schemaStatement.bindText(1, tableName);
    return schemaStatement.step();
}
bool StatisticsReader::columnExists(Database& database, string_view tableName, string_view columnName) {
    // The pragma reports a table's columns without reading any rows, so this is
    // the cheapest way to check the shape of a schema.
    const string pragmaStatement="PRAGMA table_info(" + string(tableName) + ")";
    Statement columnStatement(
        database.rawHandle(),
        pragmaStatement,
        "a check for whether a column exists"
    );
    if (!columnStatement.isValid()) {
        return false;
    }
    // The pragma returns one row per column, with the column name in the second
    // field. Scanning those rows is proportional to the number of columns, which
    // is small and bounded by the file format.
    while (columnStatement.step()) {
        if (columnStatement.columnTextView(1)==columnName) {
            return true;
        }
    }
    return false;
}
long long StatisticsReader::exactRowCount(Database& database, string_view tableName) {
    // The table name is interpolated rather than bound because SQLite does not
    // allow a table name to be a bound parameter. The name comes from a constant
    // in this codebase and never from user input, so there is nothing to inject.
    const string countStatement="SELECT COUNT(*) FROM " + string(tableName);
    Statement countingStatement(
        database.rawHandle(),
        countStatement,
        "an exact row count for a table"
    );
    if (!countingStatement.isValid() || !countingStatement.step()) {
        return 0;
    }
    return countingStatement.columnInteger(0);
}
long long StatisticsReader::pageCountFromEngine(Database& database) {
    const optional<long> reportedPageCount=database.queryPragmaInteger("PRAGMA page_count");
    return reportedPageCount.has_value() ? reportedPageCount.value() : 0LL;
}
long long StatisticsReader::freePageCountFromEngine(Database& database) {
    const optional<long> reportedFreePageCount=database.queryPragmaInteger("PRAGMA freelist_count");
    return reportedFreePageCount.has_value() ? reportedFreePageCount.value() : 0LL;
}
long long StatisticsReader::reclaimableBytesFromFreeList(const SqliteHeaderFields& headerFields) {
    if (headerFields.pageSizeBytes<=0 || headerFields.freePageCount<=0) {
        return 0;
    }
    return headerFields.freePageCount * headerFields.pageSizeBytes;
}
}
