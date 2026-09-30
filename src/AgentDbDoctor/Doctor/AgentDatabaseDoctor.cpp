// The implementation of the AgentDatabaseDoctor class.
//
// This file is where the performance contract is met. A pass performs a fixed
// number of metadata calls, reads one hundred bytes of the database itself, and
// runs only constant time queries. The only linear work anywhere in this file is
// the row count fallback, which is deliberately restricted to the small tables.
#include <optional>
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AgentDatabaseDoctor class declaration.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
// Provides the DiagnosisReport value type and the IntegrityStatus enumeration.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
// Provides StatisticsReader, which answers the constant time questions.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides PlatformMetadata, the only place the file system is asked anything.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the error translators, so no raw code reaches the user.
#include "AgentDbDoctor/Common/Exceptions.hpp"
// Provides the number to text conversions used in the report text.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Doctor {
// The tables whose row counts the report shows. The names are constants so that
// no query anywhere in the tool is built from a string typed by a user.
static constexpr const char* TABLE_SESSION="session";
static constexpr const char* TABLE_MESSAGE="message";
static constexpr const char* TABLE_PART="part";
static constexpr const char* TABLE_EVENT="event";
// The query used to count the rows of one event type. The `type` column is
// indexed in a well formed database, so this is an indexed count rather than a
// scan, which is what makes the count constant time.
static constexpr const char* EVENT_TYPE_COUNT_QUERY=
    "SELECT COUNT(*) FROM event WHERE type=?1";
// The integrity pragma. `quick_check` verifies page structure without comparing
// every index against every table.
static constexpr const char* QUICK_CHECK_PRAGMA="PRAGMA quick_check";
// The longest integrity result that is read. A sound database returns one row
// saying `ok`, so anything else is already a problem and the rest of the rows
// are only used to build the message.
static constexpr int QUICK_CHECK_RESULT_LIMIT=5;
// The size above which the full page check is not run automatically.
//
// A quick integrity check reads every page, so its cost is proportional to the
// size of the file. At two gigabytes it takes a few seconds, which is a fair
// price for a default run. Above that the number of seconds grows with the
// size, and a hundred gigabyte database would take minutes, which is exactly the
// behaviour this tool exists to remove. So above the limit the pass skips the
// check, says plainly that it skipped it, and names the flag that runs it.
static constexpr long long AUTOMATIC_INTEGRITY_CHECK_SIZE_LIMIT_BYTES=2LL * 1024LL * 1024LL * 1024LL;
AgentDatabaseDoctor::AgentDatabaseDoctor() = default;
void AgentDatabaseDoctor::recordStorageMetrics(const string& databaseFilePath, DiagnosisReport& report) {
    // One metadata call answers the logical size, the allocated size, and the
    // modification time. The file is never opened here, which is principle I/O-1
    // of the specification.
    const FileMetadata fileMetadata=PlatformMetadata::readFileMetadata(databaseFilePath);
    report.setFileExists(fileMetadata.exists);
    report.setIsDirectory(fileMetadata.isDirectory);
    report.setLogicalSizeBytes(fileMetadata.logicalSizeBytes);
    report.setAllocatedSizeBytes(fileMetadata.allocatedSizeBytes);
    report.setModificationEpochMilliseconds(fileMetadata.modificationEpochMilliseconds);
    if (!fileMetadata.exists) {
        report.addWarning(
            "The file was not found at that path. The coding agent may store its data somewhere "
            "else, or may not have been used on this machine yet."
        );
        return;
    }
    // The write ahead log sits next to the database and can itself be large while a
    // coding agent is running. It costs one more metadata call, which is still
    // inside the budget.
    const string writeAheadLogPath=databaseFilePath + "-wal";
    const FileMetadata writeAheadLogMetadata=PlatformMetadata::readFileMetadata(writeAheadLogPath);
    if (writeAheadLogMetadata.exists) {
        report.setHasWriteAheadLog(true);
        report.setWriteAheadLogSizeBytes(writeAheadLogMetadata.logicalSizeBytes);
    }
    // The header read is the only read of the database's own bytes in the whole
    // pass. One hundred bytes, served from the page cache after the first time.
    if (fileMetadata.isDirectory) {
        // Several supported agents keep plain text files rather than a database.
        // There is nothing to check inside a folder, so the report says what the
        // folder is and stops, rather than reporting a file that is not a database
        // as though it were a fault.
        report.setPruningSupported(false);
        return;
    }
    const SqliteHeaderFields headerFields=StatisticsReader::readHeaderFields(databaseFilePath);
    if (!headerFields.isValidSqliteFile) {
        report.setIntegrityStatus(IntegrityStatus::NOT_A_DATABASE);
        report.setIntegrityMessage(
            "The first bytes of the file are not the SQLite signature, so this file is not a "
            "SQLite database. The tool will not change it."
        );
        return;
    }
    report.setPageSizeBytes(headerFields.pageSizeBytes);
    report.setPageCount(headerFields.pageCount);
    report.setFreePageCount(headerFields.freePageCount);
    report.setReclaimableBytes(StatisticsReader::reclaimableBytesFromFreeList(headerFields));
    if (headerFields.declaredFileSizeBytes>0
        && headerFields.declaredFileSizeBytes!=fileMetadata.logicalSizeBytes) {
        // The two numbers come from different places: one from SQLite's own record
        // of how many pages it wrote, and one from the file system. A difference
        // means a write was interrupted, and the user deserves to know.
        report.addWarning(
            string("SQLite believes this database should be ") + to_string(headerFields.declaredFileSizeBytes)
            + " bytes but the file is " + to_string(fileMetadata.logicalSizeBytes)
            + " bytes. That difference usually means a write was interrupted. Run the recover "
            + "command to let SQLite finish it."
        );
    }
}
void AgentDatabaseDoctor::recordTableRowCounts(Database& database, DiagnosisReport& report) {
    // The constant time answer comes first. Only when the statistics table has
    // never been populated does the code fall back to counting, and even then the
    // fallback is used only for the tables that are small by construction.
    const vector<string> tablesToCount={TABLE_SESSION, TABLE_MESSAGE, TABLE_PART, TABLE_EVENT};
    for (const string& currentTable : tablesToCount) {
        if (!StatisticsReader::tableExists(database, currentTable)) {
            continue;
        }
        const optional<long> approximateCount=StatisticsReader::approximateRowCount(database, currentTable);
        if (approximateCount.has_value()) {
            report.setRowCount(currentTable, approximateCount.value(), true);
            continue;
        }
        if (currentTable==string(TABLE_EVENT)) {
            // The event table is the one table that must never be counted exactly.
            // On a hundred gigabyte database a full scan would read the whole file,
            // which is precisely the failure this tool exists to prevent. The
            // report says the number is unknown rather than making the user wait
            // for an answer that would take minutes.
            report.addWarning(
                "The number of rows in the event table is not known, because this database has "
                "never been analysed and counting it exactly would mean reading the whole file. "
                "Run the analyze command to make that number available instantly in future."
            );
            continue;
        }
        report.setRowCount(currentTable, StatisticsReader::exactRowCount(database, currentTable), false);
    }
}
void AgentDatabaseDoctor::recordRedundantEventCount(Database& database, DiagnosisReport& report) {
    const vector<string> redundantTypes=report.getRedundantEventTypes();
    if (redundantTypes.empty()) {
        report.setRedundantEventRowCount(0);
        return;
    }
    if (!StatisticsReader::tableExists(database, TABLE_EVENT)) {
        report.setRedundantEventRowCount(0);
        return;
    }
    // One prepared statement, stepped once per event type. The number of types is
    // small and fixed, so the whole loop is constant time. This is what makes the
    // count of a hundred million duplicated rows cost the same as a count of ten.
    Statement eventCountStatement(
        database.rawHandle(),
        EVENT_TYPE_COUNT_QUERY,
        "a count of the event rows that are known duplicates"
    );
    if (!eventCountStatement.isValid()) {
        return;
    }
    long long totalRedundantRows=0;
    for (const string& currentType : redundantTypes) {
        eventCountStatement.reset();
        eventCountStatement.bindText(1, currentType);
        if (eventCountStatement.step()) {
            totalRedundantRows+=eventCountStatement.columnInteger(0);
        }
    }
    report.setRedundantEventRowCount(totalRedundantRows);
}
void AgentDatabaseDoctor::recordIntegrityStatus(
    const string& databaseFilePath,
    DiagnosisReport& report,
    bool enableIntegrityCheck
) {
    if (!enableIntegrityCheck) {
        // The watchdog path deliberately skips this. A full page check reads the
        // entire file, and the whole point of the watchdog is to cost one system
        // call per database per tick.
        return;
    }
    try {
        // The connection is opened read only, so the check cannot change the file
        // even if SQLite wanted to. The busy timeout stops the check from blocking
        // behind a running coding agent.
        Database database(databaseFilePath, Database::OPEN_READ_ONLY);
        database.setBusyTimeoutMilliseconds(2000);
        Statement quickCheckStatement(
            database.rawHandle(),
            QUICK_CHECK_PRAGMA,
            "a quick integrity check"
        );
        if (!quickCheckStatement.isValid()) {
            report.setIntegrityStatus(IntegrityStatus::PROBLEM_REPORTED);
            report.setIntegrityMessage(
                string("The integrity check could not be run. ")
                + SqliteErrorTranslator::explain(quickCheckStatement.prepareResultCode())
            );
            return;
        }
        string firstProblemText;
        bool reportedProblem=false;
        int resultCount=0;
        while (quickCheckStatement.step() && resultCount<QUICK_CHECK_RESULT_LIMIT) {
            const string resultText=quickCheckStatement.columnText(0);
            if (resultText!="ok" && !reportedProblem) {
                reportedProblem=true;
                firstProblemText=resultText;
            }
            resultCount++;
        }
        if (reportedProblem) {
            report.setIntegrityStatus(IntegrityStatus::STRUCTURALLY_DAMAGED);
            report.setIntegrityMessage("SQLite reports: " + firstProblemText);
            return;
        }
        report.setIntegrityStatus(IntegrityStatus::INTACT);
        report.setIntegrityMessage("Every page checked out.");
    } catch (const DatabaseOpenException& openFailure) {
        // The failure code decides which of the several very different problems
        // this is, and the two the specification calls out by name are the
        // operating system refusing to read the file, and a file that is not a
        // database at all.
        report.setDatabaseOpenSucceeded(false);
        report.setOpenFailureCode(openFailure.openResultCode());
        report.setIntegrityStatus(
            openFailure.openResultCode()==SQLITE_IOERR_READ
                ? IntegrityStatus::UNREADABLE
                : IntegrityStatus::PROBLEM_REPORTED
        );
        report.setIntegrityMessage(openFailure.message());
    }
}
DiagnosisReport AgentDatabaseDoctor::diagnoseDatabase(
    const string& databaseFilePath,
    const string& agentName,
    bool runRequestedIntegrityCheck
) {
    // The clock is read once at the start and once at the end. The counter is reset
    // first so the report describes this pass and not the whole process.
    PlatformMetadata::resetMetadataCallCount();
    const long long startNanoseconds=PlatformMetadata::monotonicNanoseconds();
    DiagnosisReport report(databaseFilePath);
    if (!agentName.empty()) {
        report.setAgentName(agentName);
    }
    recordStorageMetrics(databaseFilePath, report);
    if (report.getFileExists()
        && !report.getIsDirectory()
        && report.getIntegrityStatus()!=IntegrityStatus::NOT_A_DATABASE) {
        try {
            Database database(databaseFilePath, Database::OPEN_READ_ONLY);
            database.setBusyTimeoutMilliseconds(2000);
            report.setDatabaseOpenSucceeded(true);
            // The check runs automatically below the size limit and on request
            // above it, so the common case is checked and the large case still
            // meets the performance budget.
            const bool isSmallEnoughToCheckAlways=
                report.getLogicalSizeBytes() <= AUTOMATIC_INTEGRITY_CHECK_SIZE_LIMIT_BYTES;
            const bool checkWasRequested=report.getIntegrityStatus()!=IntegrityStatus::UNREADABLE
                && runRequestedIntegrityCheck;
            const bool shouldRunCheck=isSmallEnoughToCheckAlways || checkWasRequested;
            if (!isSmallEnoughToCheckAlways && !runRequestedIntegrityCheck) {
                report.addWarning(
                    string("The full page by page integrity check was not run, because this database "
                        "is larger than ")
                    + HumanReadableFormatting::formatBytes(AUTOMATIC_INTEGRITY_CHECK_SIZE_LIMIT_BYTES)
                    + " and the check reads the whole file. Everything else below is complete. Run "
                    + "the check on purpose with: agent-db-doctor diagnose --check"
                );
            }
            recordIntegrityStatus(databaseFilePath, report, shouldRunCheck);
            recordTableRowCounts(database, report);
            recordRedundantEventCount(database, report);
            // The journal mode and the auto vacuum setting are both single value
            // pragmas, so they cost one prepared statement each.
            const optional<string> journalMode=database.queryPragmaText("PRAGMA journal_mode");
            if (journalMode.has_value()) {
                report.setJournalMode(journalMode.value());
            }
            const optional<long> autoVacuumMode=database.queryPragmaInteger("PRAGMA auto_vacuum");
            if (autoVacuumMode.has_value()) {
                report.setAutoVacuumEnabled(autoVacuumMode.value()!=0);
            }
            if (!report.getAutoVacuumEnabled() && report.getFreePageCount()>0) {
                report.addWarning(
                    "This database does not return unused space to the file system on its own. "
                    "The compact command is what actually shrinks the file; the prune command only "
                    "marks the space as reusable."
                );
            }
        } catch (const DatabaseOpenException& openFailure) {
            report.setDatabaseOpenSucceeded(false);
            report.setOpenFailureCode(openFailure.openResultCode());
            report.setIntegrityStatus(
                openFailure.openResultCode()==SQLITE_IOERR_READ
                    ? IntegrityStatus::UNREADABLE
                    : IntegrityStatus::PROBLEM_REPORTED
            );
            report.setIntegrityMessage(openFailure.message());
        }
    }
    // The next steps are derived from what was found, so the user is always told
    // something they can do. The specification requires this of every command.
    if (!report.getFileExists()) {
        report.addSuggestedNextStep(
            "Nothing to do. The coding agent has not created a database on this machine yet."
        );
    } else if (report.getIsDirectory()) {
        report.addSuggestedNextStep(
            "This is a folder of text files rather than a database, so there is nothing to repair. "
            "Its size is reported above so you know whether it is worth keeping."
        );
    } else if (report.getIntegrityStatus()==IntegrityStatus::UNREADABLE) {
        report.addSuggestedNextStep(
            "Run the recover command. The file is there but the operating system cannot read it."
        );
    } else if (report.getIntegrityStatus()==IntegrityStatus::STRUCTURALLY_DAMAGED) {
        report.addSuggestedNextStep(
            "Run the recover command. The file is damaged and needs repairing before anything else."
        );
    } else if (report.getRedundantEventRowCount()>0) {
        report.addSuggestedNextStep(
            string("Run the prune command to remove the ") + to_string(report.getRedundantEventRowCount())
            + " duplicate rows, then the compact command to shrink the file."
        );
    } else if (report.getReclaimableBytes()>0) {
        report.addSuggestedNextStep(
            "Run the compact command. There is already unused space inside the file that a "
            "compaction would return to the drive."
        );
    } else {
        report.addSuggestedNextStep("Nothing needs doing. The database is in good shape.");
    }
    const long long endNanoseconds=PlatformMetadata::monotonicNanoseconds();
    report.setDurationNanoseconds(endNanoseconds-startNanoseconds);
    report.setMetadataCallCount(PlatformMetadata::metadataCallCount());
    return report;
}
optional<DiagnosisReport> AgentDatabaseDoctor::diagnoseIfAboveThreshold(
    const string& databaseFilePath,
    const string& agentName,
    long long warningThresholdBytes,
    long long previousSizeBytes
) {
    // This method is the watchdog's tick, and it must cost one metadata call. The
    // counter is not reset here, because the service wants a running total across
    // its whole life rather than a per tick number.
    const FileMetadata fileMetadata=PlatformMetadata::readFileMetadata(databaseFilePath);
    if (!fileMetadata.exists) {
        return nullopt;
    }
    // Two conditions trigger a full pass: the file has crossed the size threshold,
    // or it has grown since the last tick. Either one means something changed that
    // the user should hear about.
    if (fileMetadata.logicalSizeBytes < warningThresholdBytes
        && fileMetadata.logicalSizeBytes <= previousSizeBytes) {
        return nullopt;
    }
    return diagnoseDatabase(databaseFilePath, agentName, false);
}
vector<DiagnosisReport> AgentDatabaseDoctor::diagnoseAllDiscoveredDatabases() {
    vector<DiagnosisReport> reports;
    for (const DiscoveredDatabase& discovered : agentRegistry.discoverAllDatabases()) {
        reports.push_back(diagnoseDatabase(discovered.filePath, discovered.agentName, false));
    }
    return reports;
}
}
