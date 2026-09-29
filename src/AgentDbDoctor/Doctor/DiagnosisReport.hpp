// The DiagnosisReport value type, which is everything a diagnostic pass found out
// about one database.
//
// It is a plain class with private fields and methods rather than a base class or
// a structure with public fields, so that the report cannot be half filled in by
// accident. Every field has a method that reads it and a method that writes it,
// which is the same shape a Java developer would expect from a class with
// getters and setters.
#ifndef AGENT_DB_DOCTOR_DOCTOR_DIAGNOSISREPORT_HPP
#define AGENT_DB_DOCTOR_DOCTOR_DIAGNOSISREPORT_HPP
// Provides `map`, an ordered dictionary keyed by a string.
#include <map>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Doctor {
// The IntegrityStatus enumeration says what a quick integrity check found.
//
// An `enum class` is a strongly typed enumeration whose values do not silently
// convert to `int`, so a mistyped value cannot compile.
enum class IntegrityStatus {
    // The check has not been run.
    NOT_CHECKED,
    // Every page checked out. This is the good answer.
    INTACT,
    // The file is not a SQLite database, or its header is unreadable.
    NOT_A_DATABASE,
    // SQLite reported damage in the structure of the database.
    STRUCTURALLY_DAMAGED,
    // The operating system refused to read the file, which is the state the
    // specification calls `ERROR_READ_FAULT`. It usually means the file was
    // compressed or truncated while the database was open.
    UNREADABLE,
    // SQLite reported a problem that is none of the above.
    PROBLEM_REPORTED
};
// The DiagnosisReport class holds everything one diagnostic pass learned.
//
// The class is `final` because the specification forbids inheritance. A report is
// a value: it is built by the doctor, passed to the presentation layer, and
// thrown away when the command ends.
class DiagnosisReport final {
public:
    // Builds a report for the database at `databaseFilePath`.
    explicit DiagnosisReport(string databaseFilePath);
    // The path this report is about.
    const string& getDatabaseFilePath() const;
    // Records which agent owns the database.
    void setAgentName(string agentName);
    string getAgentName() const;
    // Records whether the file was found at all.
    void setFileExists(bool exists);
    bool getFileExists() const;
    // Records the size the file would occupy if every byte were stored.
    void setLogicalSizeBytes(long long sizeBytes);
    long long getLogicalSizeBytes() const;
    // Records the size the file actually occupies on the volume, which differs
    // from the logical size when the volume compresses or when the file is sparse.
    void setAllocatedSizeBytes(long long sizeBytes);
    long long getAllocatedSizeBytes() const;
    // Records the size of the write ahead log that sits next to the database.
    void setWriteAheadLogSizeBytes(long long sizeBytes);
    long long getWriteAheadLogSizeBytes() const;
    // Records whether a write ahead log exists.
    void setHasWriteAheadLog(bool hasLog);
    bool getHasWriteAheadLog() const;
    // Records when the database was last written, as milliseconds since 1970.
    void setModificationEpochMilliseconds(long long epochMilliseconds);
    long long getModificationEpochMilliseconds() const;
    // Records the page size and page counts read from the file header.
    void setPageSizeBytes(long long sizeBytes);
    long long getPageSizeBytes() const;
    void setPageCount(long long count);
    long long getPageCount() const;
    void setFreePageCount(long long count);
    long long getFreePageCount() const;
    // Records the space a compaction would give back, which is the free pages
    // multiplied by the page size.
    void setReclaimableBytes(long long sizeBytes);
    long long getReclaimableBytes() const;
    // Records how the database stores its journal, for example `wal` or `delete`.
    void setJournalMode(string mode);
    string getJournalMode() const;
    // Records whether the database returns unused space to the file system on its
    // own. When this is off, only a compaction will shrink the file.
    void setAutoVacuumEnabled(bool isEnabled);
    bool getAutoVacuumEnabled() const;
    // Records the row counts, keyed by table name. A count that is approximate
    // comes from SQLite's statistics table; a count that is exact came from a full
    // scan. The report says which is which through `isRowCountApproximate`.
    void setRowCount(const string& tableName, long long rowCount, bool isApproximate);
    long long rowCount(const string& tableName) const;
    bool hasRowCount(const string& tableName) const;
    bool isRowCountApproximate(const string& tableName) const;
    vector<string> countedTableNames() const;
    // Records the number of rows in the event table that are known duplicates.
    void setRedundantEventRowCount(long long rowCount);
    long long getRedundantEventRowCount() const;
    // Records the event type names that count as duplicates for this agent.
    void setRedundantEventTypes(vector<string> typeNames);
    vector<string> getRedundantEventTypes() const;
    // Records whether this database can be pruned at all.
    void setPruningSupported(bool isSupported);
    bool getPruningSupported() const;
    // Records what the quick integrity check found.
    void setIntegrityStatus(IntegrityStatus status);
    IntegrityStatus getIntegrityStatus() const;
    // Records the exact sentence SQLite produced for a damage report, so the user
    // can see the tool's own words alongside the translated one.
    void setIntegrityMessage(string message);
    string getIntegrityMessage() const;
    // Records the SQLite result code that prevented the database being opened.
    void setOpenFailureCode(int sqliteResultCode);
    int getOpenFailureCode() const;
    // Adds a warning. Warnings are things the user should know but that do not
    // stop the command.
    void addWarning(string warning);
    vector<string> getWarnings() const;
    // Adds a suggested next step. The specification requires the tool to finish
    // every command with a short list of things the user could do next.
    void addSuggestedNextStep(string suggestedStep);
    vector<string> getSuggestedNextSteps() const;
    // Records how long the diagnostic pass took, in nanoseconds, measured with a
    // monotonic clock so a system clock change cannot distort it.
    void setDurationNanoseconds(long long nanoseconds);
    long long getDurationNanoseconds() const;
    // Records how many file metadata system calls this pass made, which is the
    // number the performance contract bounds.
    void setMetadataCallCount(long long callCount);
    long long getMetadataCallCount() const;
    // Records whether the database could be opened at all.
    void setDatabaseOpenSucceeded(bool succeeded);
    bool getDatabaseOpenSucceeded() const;
    // True when this report describes a database that is not damaged. A report for
    // a file that does not exist is not a healthy report, and the report layer
    // words the difference.
    bool isHealthy() const;
private:
    string databaseFilePath;
    string agentName;
    bool fileExists=false;
    bool databaseOpenSucceeded=false;
    long long logicalSizeBytes=0;
    long long allocatedSizeBytes=0;
    long long writeAheadLogSizeBytes=0;
    bool hasWriteAheadLog=false;
    long long modificationEpochMilliseconds=0;
    long long pageSizeBytes=0;
    long long pageCount=0;
    long long freePageCount=0;
    long long reclaimableBytes=0;
    string journalMode;
    bool autoVacuumEnabled=false;
    long long redundantEventRowCount=0;
    bool pruningSupported=false;
    IntegrityStatus integrityStatus=IntegrityStatus::NOT_CHECKED;
    string integrityMessage;
    int openFailureCode=0;
    long long durationNanoseconds=0;
    long long metadataCallCount=0;
    // The row counts, keyed by table name. A `map` keeps them sorted, so a report
    // lists the same tables in the same order on every machine.
    map<string, long long> rowCounts;
    // Which of the row counts came from the statistics table rather than from a
    // full scan. The set is parallel to the row count map.
    map<string, bool> approximateRowCounts;
    vector<string> redundantTypeNames;
    vector<string> warningMessages;
    vector<string> nextStepMessages;
};
}
#endif
